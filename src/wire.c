#include "thalovant/wire.h"

#include <stdio.h>
#include <string.h>

#include "thalovant/codec.h"
#include "thalovant/json.h"

/* ------------------------------------------------------------ msg types */

typedef struct {
    const char *name;
    int id;
} tlv_msg_type;

static const tlv_msg_type MSG_TYPES[] = {
    { "shake", 0 },     { "handshake", 0 }, { "bus", 1 },      { "shared_bus", 2 },
    { "broadcast", 3 }, { "propagate", 4 }, { "escalate", 5 }, { "hello", 6 },
    { "query", 7 },     { "cascade", 8 },   { "ping", 9 },     { "rendezvous", 10 },
    { "3rdparty", 11 }, { "bin", 12 },
};

int thalovant_wire_msg_type_id(const char *msg_type)
{
    if (msg_type == NULL) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(MSG_TYPES) / sizeof(MSG_TYPES[0]); i++) {
        if (strcmp(MSG_TYPES[i].name, msg_type) == 0) {
            return MSG_TYPES[i].id;
        }
    }
    return -1;
}

const char *thalovant_wire_msg_type_name(int id)
{
    /* "shake" (not "handshake") is canonical for id 0, like the Node SDK. */
    for (size_t i = 0; i < sizeof(MSG_TYPES) / sizeof(MSG_TYPES[0]); i++) {
        if (MSG_TYPES[i].id == id) {
            return MSG_TYPES[i].name;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ appenders */

static int append(char *out, size_t cap, size_t *pos, const char *text)
{
    size_t len = strlen(text);
    if (*pos + len + 1 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';
    return THALOVANT_OK;
}

/* JSON string literal with the same escaping JSON.stringify applies. */
static int append_json_string(char *out, size_t cap, size_t *pos, const char *text)
{
    if (*pos + 1 >= cap) {
        return THALOVANT_ERR_NOMEM;
    }
    out[(*pos)++] = '"';
    for (const char *c = text; *c != '\0'; c++) {
        unsigned char ch = (unsigned char)*c;
        char buf[8];
        const char *piece = buf;
        switch (ch) {
        case '"': piece = "\\\""; break;
        case '\\': piece = "\\\\"; break;
        case '\b': piece = "\\b"; break;
        case '\f': piece = "\\f"; break;
        case '\n': piece = "\\n"; break;
        case '\r': piece = "\\r"; break;
        case '\t': piece = "\\t"; break;
        default:
            if (ch < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04x", ch);
            } else {
                buf[0] = (char)ch;
                buf[1] = '\0';
            }
            break;
        }
        int rc = append(out, cap, pos, piece);
        if (rc != THALOVANT_OK) {
            return rc;
        }
    }
    if (*pos + 2 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    out[(*pos)++] = '"';
    out[*pos] = '\0';
    return THALOVANT_OK;
}

static int append_string_or_null(char *out, size_t cap, size_t *pos, const char *value)
{
    if (value == NULL) {
        return append(out, cap, pos, "null");
    }
    return append_json_string(out, cap, pos, value);
}

/* ---------------------------------------------------------- serializing */

int thalovant_wire_serialize(const thalovant_hive_message *msg, char *out, size_t cap)
{
    if (msg == NULL || msg->msg_type == NULL || out == NULL || cap == 0) {
        return THALOVANT_ERR_INVALID;
    }
    size_t pos = 0;
    int rc;
    if ((rc = append(out, cap, &pos, "{\"msg_type\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, msg->msg_type)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"payload\":")) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, msg->payload_json ? msg->payload_json : "{}")) != THALOVANT_OK)
        return rc;
    if ((rc = append(out, cap, &pos, ",\"metadata\":")) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, msg->metadata_json ? msg->metadata_json : "{}")) !=
        THALOVANT_OK)
        return rc;
    if ((rc = append(out, cap, &pos, ",\"route\":")) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, msg->route_json ? msg->route_json : "[]")) != THALOVANT_OK)
        return rc;
    if ((rc = append(out, cap, &pos, ",\"node\":")) != THALOVANT_OK) return rc;
    if ((rc = append_string_or_null(out, cap, &pos, msg->node)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"target_site_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_string_or_null(out, cap, &pos, msg->target_site_id)) != THALOVANT_OK)
        return rc;
    if ((rc = append(out, cap, &pos, ",\"target_pubkey\":")) != THALOVANT_OK) return rc;
    if ((rc = append_string_or_null(out, cap, &pos, msg->target_pubkey)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"source_peer\":")) != THALOVANT_OK) return rc;
    if ((rc = append_string_or_null(out, cap, &pos, msg->source_peer)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, "}")) != THALOVANT_OK) return rc;
    return (int)pos;
}

/* -------------------------------------------------------------- parsing */

static int copy_optional_string(const char *js, const thalovant_json_tok *toks, int count, int obj,
                                const char *key, char *out, size_t cap, bool *present)
{
    *present = false;
    int value = thalovant_json_object_get(js, toks, count, obj, key);
    if (value < 0 || toks[value].type != THALOVANT_JSON_STRING) {
        return THALOVANT_OK; /* absent, null, or non-string: leave unset */
    }
    int len = thalovant_json_unescape(js, &toks[value], out, cap);
    if (len < 0) {
        return len;
    }
    *present = true;
    return THALOVANT_OK;
}

int thalovant_wire_parse(const char *json, size_t len, thalovant_wire_frame *out)
{
    if (json == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    thalovant_json_tok toks[THALOVANT_WIRE_MAX_TOKENS];
    int count = thalovant_json_parse(json, len, toks, THALOVANT_WIRE_MAX_TOKENS);
    if (count < 0) {
        return count;
    }
    if (toks[0].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }
    int msg_type = thalovant_json_object_get(json, toks, count, 0, "msg_type");
    if (msg_type < 0 || toks[msg_type].type != THALOVANT_JSON_STRING) {
        return THALOVANT_ERR_MISSING;
    }
    int rc = thalovant_json_unescape(json, &toks[msg_type], out->msg_type, sizeof(out->msg_type));
    if (rc < 0) {
        return rc;
    }
    int payload = thalovant_json_object_get(json, toks, count, 0, "payload");
    if (payload >= 0) {
        int start, end;
        thalovant_json_raw_span(&toks[payload], &start, &end);
        out->payload = json + start;
        out->payload_len = (size_t)(end - start);
    }
    int metadata = thalovant_json_object_get(json, toks, count, 0, "metadata");
    if (metadata >= 0) {
        int start, end;
        thalovant_json_raw_span(&toks[metadata], &start, &end);
        out->metadata = json + start;
        out->metadata_len = (size_t)(end - start);
    }
    rc = copy_optional_string(json, toks, count, 0, "node", out->node, sizeof(out->node),
                              &out->has_node);
    if (rc != THALOVANT_OK) return rc;
    rc = copy_optional_string(json, toks, count, 0, "target_site_id", out->target_site_id,
                              sizeof(out->target_site_id), &out->has_target_site_id);
    if (rc != THALOVANT_OK) return rc;
    rc = copy_optional_string(json, toks, count, 0, "target_pubkey", out->target_pubkey,
                              sizeof(out->target_pubkey), &out->has_target_pubkey);
    if (rc != THALOVANT_OK) return rc;
    rc = copy_optional_string(json, toks, count, 0, "source_peer", out->source_peer,
                              sizeof(out->source_peer), &out->has_source_peer);
    if (rc != THALOVANT_OK) return rc;
    return THALOVANT_OK;
}

bool thalovant_wire_is_encrypted(const char *json, size_t len)
{
    thalovant_json_tok toks[THALOVANT_WIRE_MAX_TOKENS];
    int count = thalovant_json_parse(json, len, toks, THALOVANT_WIRE_MAX_TOKENS);
    if (count < 0 || toks[0].type != THALOVANT_JSON_OBJECT) {
        return false;
    }
    return thalovant_json_object_get(json, toks, count, 0, "ciphertext") >= 0;
}

bool thalovant_wire_is_preshared_handshake(const thalovant_wire_frame *frame)
{
    if (frame == NULL || frame->payload == NULL) {
        return false;
    }
    if (strcmp(frame->msg_type, "handshake") != 0 && strcmp(frame->msg_type, "shake") != 0) {
        return false;
    }
    thalovant_json_tok toks[64];
    int count = thalovant_json_parse(frame->payload, frame->payload_len, toks, 64);
    if (count < 0 || toks[0].type != THALOVANT_JSON_OBJECT) {
        return false;
    }
    int preshared = thalovant_json_object_get(frame->payload, toks, count, 0, "preshared_key");
    if (preshared < 0 || !thalovant_json_is_truthy(frame->payload, &toks[preshared])) {
        return false;
    }
    int handshake = thalovant_json_object_get(frame->payload, toks, count, 0, "handshake");
    if (handshake >= 0 && thalovant_json_is_truthy(frame->payload, &toks[handshake])) {
        return false;
    }
    int envelope = thalovant_json_object_get(frame->payload, toks, count, 0, "envelope");
    if (envelope >= 0 && thalovant_json_is_truthy(frame->payload, &toks[envelope])) {
        return false;
    }
    return true;
}

/* ----------------------------------------------------- crypto envelopes */

int thalovant_envelope_encrypt_json(const uint8_t key[16],
                                    const uint8_t nonce[THALOVANT_GCM_NONCE_LEN],
                                    uint8_t *plaintext, size_t plaintext_len, char *out,
                                    size_t cap)
{
    if (out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    uint8_t tag[THALOVANT_GCM_TAG_LEN];
    int rc = thalovant_aes_gcm_encrypt(key, nonce, THALOVANT_GCM_NONCE_LEN, NULL, 0, plaintext,
                                       plaintext_len, plaintext, tag);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    /* {"ciphertext":"..","tag":"..","nonce":".."} */
    size_t needed = 2 * plaintext_len + 32 + 32 + 45 + 1;
    if (cap < needed) {
        return THALOVANT_ERR_NOMEM;
    }
    size_t pos = 0;
    if ((rc = append(out, cap, &pos, "{\"ciphertext\":\"")) != THALOVANT_OK) return rc;
    rc = thalovant_hex_encode(plaintext, plaintext_len, out + pos, cap - pos);
    if (rc < 0) return rc;
    pos += (size_t)rc;
    if ((rc = append(out, cap, &pos, "\",\"tag\":\"")) != THALOVANT_OK) return rc;
    rc = thalovant_hex_encode(tag, sizeof(tag), out + pos, cap - pos);
    if (rc < 0) return rc;
    pos += (size_t)rc;
    if ((rc = append(out, cap, &pos, "\",\"nonce\":\"")) != THALOVANT_OK) return rc;
    rc = thalovant_hex_encode(nonce, THALOVANT_GCM_NONCE_LEN, out + pos, cap - pos);
    if (rc < 0) return rc;
    pos += (size_t)rc;
    if ((rc = append(out, cap, &pos, "\"}")) != THALOVANT_OK) return rc;
    return (int)pos;
}

/*
 * Node's detectJsonEncoding: hex when the nonce is even-length all-hex and
 * decodes to 16 or 12 bytes; Base64 otherwise. The verdict applies to all
 * three envelope fields.
 */
static bool nonce_looks_hex(const char *nonce, size_t len)
{
    if (len == 0 || len % 2 != 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = nonce[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    size_t bytes = len / 2;
    return bytes == 16 || bytes == 12;
}

typedef struct {
    char text[128];
    size_t len;
} tlv_small_field;

static int envelope_field(const char *js, const thalovant_json_tok *toks, int count,
                          const char *key, tlv_small_field *out)
{
    int value = thalovant_json_object_get(js, toks, count, 0, key);
    if (value < 0 || toks[value].type != THALOVANT_JSON_STRING) {
        return THALOVANT_ERR_MISSING;
    }
    int len = thalovant_json_unescape(js, &toks[value], out->text, sizeof(out->text));
    if (len < 0) {
        return len;
    }
    out->len = (size_t)len;
    return THALOVANT_OK;
}

int thalovant_envelope_decrypt_json(const uint8_t key[16], const char *envelope_json, size_t len,
                                    uint8_t *out, size_t cap, size_t *out_len)
{
    if (envelope_json == NULL || out == NULL || out_len == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    thalovant_json_tok toks[32];
    int count = thalovant_json_parse(envelope_json, len, toks, 32);
    if (count < 0) {
        return count;
    }
    if (toks[0].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }
    tlv_small_field nonce_field, tag_field;
    int rc = envelope_field(envelope_json, toks, count, "nonce", &nonce_field);
    if (rc != THALOVANT_OK) return rc;
    rc = envelope_field(envelope_json, toks, count, "tag", &tag_field);
    if (rc != THALOVANT_OK) return rc;
    int ct_tok = thalovant_json_object_get(envelope_json, toks, count, 0, "ciphertext");
    if (ct_tok < 0 || toks[ct_tok].type != THALOVANT_JSON_STRING) {
        return THALOVANT_ERR_MISSING;
    }
    const char *ct_text = envelope_json + toks[ct_tok].start;
    size_t ct_text_len = (size_t)(toks[ct_tok].end - toks[ct_tok].start);

    bool hex = nonce_looks_hex(nonce_field.text, nonce_field.len);
    uint8_t nonce[THALOVANT_GCM_NONCE_LEN];
    uint8_t tag[THALOVANT_GCM_TAG_LEN];
    int nonce_len, tag_len, ct_len;
    if (hex) {
        nonce_len = thalovant_hex_decode(nonce_field.text, nonce_field.len, nonce, sizeof(nonce));
        tag_len = thalovant_hex_decode(tag_field.text, tag_field.len, tag, sizeof(tag));
        ct_len = thalovant_hex_decode(ct_text, ct_text_len, out, cap);
    } else {
        nonce_len =
            thalovant_base64_decode(nonce_field.text, nonce_field.len, nonce, sizeof(nonce));
        tag_len = thalovant_base64_decode(tag_field.text, tag_field.len, tag, sizeof(tag));
        ct_len = thalovant_base64_decode(ct_text, ct_text_len, out, cap);
    }
    if (nonce_len < 0 || tag_len < 0 || ct_len < 0) {
        int worst = nonce_len < 0 ? nonce_len : (tag_len < 0 ? tag_len : ct_len);
        return worst;
    }
    if (tag_len != THALOVANT_GCM_TAG_LEN || nonce_len == 0) {
        return THALOVANT_ERR_INVALID;
    }
    rc = thalovant_aes_gcm_decrypt(key, nonce, (size_t)nonce_len, NULL, 0, out, (size_t)ct_len,
                                   tag, out);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    *out_len = (size_t)ct_len;
    if ((size_t)ct_len < cap) {
        out[ct_len] = '\0';
    }
    return THALOVANT_OK;
}

int thalovant_envelope_encrypt_binary(const uint8_t key[16],
                                      const uint8_t nonce[THALOVANT_GCM_NONCE_LEN],
                                      const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                                      size_t cap, size_t *out_len)
{
    if (out == NULL || out_len == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    size_t total = THALOVANT_GCM_NONCE_LEN + plaintext_len + THALOVANT_GCM_TAG_LEN;
    if (cap < total) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out, nonce, THALOVANT_GCM_NONCE_LEN);
    int rc = thalovant_aes_gcm_encrypt(key, nonce, THALOVANT_GCM_NONCE_LEN, NULL, 0, plaintext,
                                       plaintext_len, out + THALOVANT_GCM_NONCE_LEN,
                                       out + THALOVANT_GCM_NONCE_LEN + plaintext_len);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    *out_len = total;
    return THALOVANT_OK;
}

int thalovant_envelope_decrypt_binary(const uint8_t key[16], const uint8_t *in, size_t len,
                                      uint8_t *out, size_t cap, size_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    /* Mirrors the Node SDK: payloads of nonce+tag size or less are invalid. */
    if (len <= THALOVANT_GCM_NONCE_LEN + THALOVANT_GCM_TAG_LEN) {
        return THALOVANT_ERR_INVALID;
    }
    size_t ct_len = len - THALOVANT_GCM_NONCE_LEN - THALOVANT_GCM_TAG_LEN;
    if (cap < ct_len) {
        return THALOVANT_ERR_NOMEM;
    }
    int rc = thalovant_aes_gcm_decrypt(key, in, THALOVANT_GCM_NONCE_LEN, NULL, 0,
                                       in + THALOVANT_GCM_NONCE_LEN, ct_len,
                                       in + THALOVANT_GCM_NONCE_LEN + ct_len, out);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    *out_len = ct_len;
    return THALOVANT_OK;
}

/* ------------------------------------------------------------ handshake */

int thalovant_wire_hello(const char *pubkey, const char *session_id, const char *site_id,
                         char *out, size_t cap)
{
    if (session_id == NULL || site_id == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    char payload[512];
    size_t pos = 0;
    int rc;
    if ((rc = append(payload, sizeof(payload), &pos, "{\"pubkey\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(payload, sizeof(payload), &pos, pubkey ? pubkey : "")) !=
        THALOVANT_OK)
        return rc;
    if ((rc = append(payload, sizeof(payload), &pos, ",\"session\":{\"session_id\":")) !=
        THALOVANT_OK)
        return rc;
    if ((rc = append_json_string(payload, sizeof(payload), &pos, session_id)) != THALOVANT_OK)
        return rc;
    if ((rc = append(payload, sizeof(payload), &pos, "},\"site_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(payload, sizeof(payload), &pos, site_id)) != THALOVANT_OK)
        return rc;
    if ((rc = append(payload, sizeof(payload), &pos, "}")) != THALOVANT_OK) return rc;

    thalovant_hive_message msg = { "hello", payload, NULL, NULL, NULL, NULL, NULL, NULL };
    return thalovant_wire_serialize(&msg, out, cap);
}

int thalovant_wire_authorization(const char *user_agent, const char *access_key, char *out,
                                 size_t cap)
{
    if (user_agent == NULL || access_key == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    char joined[512];
    int written = snprintf(joined, sizeof(joined), "%s:%s", user_agent, access_key);
    if (written < 0 || (size_t)written >= sizeof(joined)) {
        return THALOVANT_ERR_NOMEM;
    }
    return thalovant_base64_encode((const uint8_t *)joined, (size_t)written, out, cap);
}

static bool url_unreserved(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

int thalovant_wire_ws_url(const char *endpoint, const char *authorization, char *out, size_t cap)
{
    if (endpoint == NULL || authorization == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    size_t pos = 0;
    int rc = append(out, cap, &pos, endpoint);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    rc = append(out, cap, &pos,
                strchr(endpoint, '?') != NULL ? "&authorization=" : "?authorization=");
    if (rc != THALOVANT_OK) {
        return rc;
    }
    for (const char *c = authorization; *c != '\0'; c++) {
        char piece[4];
        if (url_unreserved(*c)) {
            piece[0] = *c;
            piece[1] = '\0';
        } else {
            snprintf(piece, sizeof(piece), "%%%02X", (unsigned char)*c);
        }
        rc = append(out, cap, &pos, piece);
        if (rc != THALOVANT_OK) {
            return rc;
        }
    }
    return (int)pos;
}

/* --------------------------------------------------------- binary frame */

int thalovant_wire_encode_binary(const thalovant_hive_message *msg, uint8_t *out, size_t cap,
                                 size_t *out_len)
{
    if (msg == NULL || msg->msg_type == NULL || out == NULL || out_len == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    int type_id = thalovant_wire_msg_type_id(msg->msg_type);
    if (type_id < 0) {
        type_id = 11; /* 3rdparty */
    }
    const char *metadata = msg->metadata_json ? msg->metadata_json : "{}";
    const char *payload = msg->payload_json ? msg->payload_json : "{}";
    size_t metadata_len = strlen(metadata);
    size_t payload_len = strlen(payload);
    if (metadata_len > 255) {
        return THALOVANT_ERR_INVALID;
    }
    size_t total = 2 + metadata_len + payload_len;
    if (cap < total) {
        return THALOVANT_ERR_NOMEM;
    }
    out[0] = (uint8_t)(0x80 | ((type_id & 0x1f) << 1));
    out[1] = (uint8_t)metadata_len;
    memcpy(out + 2, metadata, metadata_len);
    memcpy(out + 2 + metadata_len, payload, payload_len);
    *out_len = total;
    return THALOVANT_OK;
}

typedef struct {
    const uint8_t *data;
    size_t bit_len;
    size_t bit_pos;
} tlv_bit_reader;

static int read_bit(tlv_bit_reader *reader)
{
    if (reader->bit_pos >= reader->bit_len) {
        return -1;
    }
    uint8_t byte = reader->data[reader->bit_pos / 8];
    int bit = (byte >> (7 - (reader->bit_pos % 8))) & 1;
    reader->bit_pos++;
    return bit;
}

static long read_uint(tlv_bit_reader *reader, int width)
{
    long value = 0;
    for (int i = 0; i < width; i++) {
        int bit = read_bit(reader);
        if (bit < 0) {
            return -1;
        }
        value = (value << 1) | bit;
    }
    return value;
}

int thalovant_wire_decode_binary(const uint8_t *frame, size_t len, char *metadata,
                                 size_t metadata_cap, char *payload, size_t payload_cap,
                                 thalovant_wire_binary_frame *out)
{
    if (frame == NULL || metadata == NULL || payload == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    tlv_bit_reader reader = { frame, len * 8, 0 };
    /* Skip left padding: scan until the first set bit (consumed). */
    int bit;
    do {
        bit = read_bit(&reader);
        if (bit < 0) {
            return THALOVANT_ERR_INVALID;
        }
    } while (bit == 0);
    int versioned = read_bit(&reader);
    if (versioned < 0) {
        return THALOVANT_ERR_INVALID;
    }
    if (versioned == 1) {
        long version = read_uint(&reader, 8);
        if (version < 0) {
            return THALOVANT_ERR_INVALID;
        }
        if (version > 1) {
            return THALOVANT_ERR_UNSUPPORTED;
        }
    }
    long type_id = read_uint(&reader, 5);
    int compressed = read_bit(&reader);
    long metadata_len = read_uint(&reader, 8);
    if (type_id < 0 || compressed < 0 || metadata_len < 0) {
        return THALOVANT_ERR_INVALID;
    }
    if (compressed == 1) {
        return THALOVANT_ERR_UNSUPPORTED; /* zlib inflate is out of scope */
    }
    if ((size_t)metadata_len + 1 > metadata_cap) {
        return THALOVANT_ERR_NOMEM;
    }
    for (long i = 0; i < metadata_len; i++) {
        long byte = read_uint(&reader, 8);
        if (byte < 0) {
            return THALOVANT_ERR_INVALID;
        }
        metadata[i] = (char)byte;
    }
    metadata[metadata_len] = '\0';
    size_t remaining = (reader.bit_len - reader.bit_pos) / 8;
    if (remaining + 1 > payload_cap) {
        return THALOVANT_ERR_NOMEM;
    }
    for (size_t i = 0; i < remaining; i++) {
        long byte = read_uint(&reader, 8);
        if (byte < 0) {
            return THALOVANT_ERR_INVALID;
        }
        payload[i] = (char)byte;
    }
    payload[remaining] = '\0';
    out->type_id = (int)type_id;
    const char *name = thalovant_wire_msg_type_name((int)type_id);
    out->msg_type = name != NULL ? name : "3rdparty";
    out->metadata_len = (size_t)metadata_len;
    out->payload_len = remaining;
    return THALOVANT_OK;
}
