/*
 * HiveMessage wire envelope — serialization, parsing, encryption envelopes,
 * and the WSS handshake helpers, mirroring the Node SDK's transport-core,
 * crypto, and wire modules.
 *
 * Frame shape (explicit nulls included, key order matches the Node SDK so
 * serialized frames are byte-comparable):
 *
 *   {"msg_type":"...","payload":{...},"metadata":{...},"route":[],
 *    "node":null,"target_site_id":null,"target_pubkey":null,
 *    "source_peer":null}
 *
 * Encrypted JSON envelope (hex-encoded, 16-byte nonce):
 *
 *   {"ciphertext":"<hex>","tag":"<hex>","nonce":"<hex>"}
 *
 * On decode the field encoding is auto-detected from the nonce exactly as
 * the Node SDK does: an even-length all-hex nonce of 16 or 12 bytes means
 * hex, anything else means Base64 — and the detected codec is applied to
 * all three fields.
 *
 * Binary envelope (MQTT transport): nonce(16) || ciphertext || tag(16).
 *
 * WSS handshake sequence (from transport-core.ts):
 *   1. Connect to "<wss endpoint>?authorization=" +
 *      urlencode(base64("<user agent>:<access_key>")).
 *   2. The hub sends a "handshake"/"shake" frame whose payload carries
 *      `preshared_key` (and neither `handshake` nor `envelope`).
 *   3. The satellite replies with a *plaintext* "hello" frame carrying
 *      pubkey, session.session_id, and site_id.
 *   4. All subsequent frames are sealed with AES-128-GCM using the
 *      identity crypto_key (JSON envelope on WSS, binary on MQTT).
 */
#ifndef THALOVANT_WIRE_H
#define THALOVANT_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thalovant/aes_gcm.h"
#include "thalovant/config.h"
#include "thalovant/error.h"

/* ------------------------------------------------------------- messages */

typedef struct {
    const char *msg_type;       /* required, e.g. "bus", "hello" */
    const char *payload_json;   /* raw JSON object; NULL means "{}" */
    const char *metadata_json;  /* raw JSON object; NULL means "{}" */
    const char *route_json;     /* raw JSON array; NULL means "[]" */
    const char *node;           /* NULL serializes as JSON null */
    const char *target_site_id; /* NULL serializes as JSON null */
    const char *target_pubkey;  /* NULL serializes as JSON null */
    const char *source_peer;    /* NULL serializes as JSON null */
} thalovant_hive_message;

/* Serialize to the exact frame shape above. Returns the length written. */
int thalovant_wire_serialize(const thalovant_hive_message *msg, char *out, size_t cap);

typedef struct {
    char msg_type[THALOVANT_MSG_TYPE_MAX];
    /* Raw JSON slices into the input buffer; NULL when the key is absent. */
    const char *payload;
    size_t payload_len;
    const char *metadata;
    size_t metadata_len;
    /* Optional scalar routing fields; has_* is false when absent or null. */
    bool has_node;
    char node[THALOVANT_WIRE_NODE_MAX];
    bool has_target_site_id;
    char target_site_id[THALOVANT_SITE_ID_MAX];
    bool has_target_pubkey;
    char target_pubkey[THALOVANT_PUBLIC_KEY_MAX];
    bool has_source_peer;
    char source_peer[THALOVANT_WIRE_NODE_MAX];
} thalovant_wire_frame;

/* Parse a plaintext JSON frame. The slices point into `json`. */
int thalovant_wire_parse(const char *json, size_t len, thalovant_wire_frame *out);

/* True when the top-level JSON object carries a "ciphertext" key. */
bool thalovant_wire_is_encrypted(const char *json, size_t len);

/*
 * True for a "handshake"/"shake" frame whose payload has a truthy
 * `preshared_key` and neither `handshake` nor `envelope` — the only
 * handshake this library (like the Node SDK alpha) supports. Answer it
 * with a plaintext hello frame.
 */
bool thalovant_wire_is_preshared_handshake(const thalovant_wire_frame *frame);

/* ------------------------------------------------------ crypto envelopes */

/*
 * Seal `plaintext` (overwritten in place with ciphertext during sealing —
 * pass a scratch copy if you need to keep it) into the hex JSON envelope.
 * The caller supplies the 16-byte nonce from its platform RNG; never reuse
 * a nonce under the same key. Returns the envelope length.
 */
int thalovant_envelope_encrypt_json(const uint8_t key[16],
                                    const uint8_t nonce[THALOVANT_GCM_NONCE_LEN],
                                    uint8_t *plaintext, size_t plaintext_len, char *out,
                                    size_t cap);

/*
 * Open a {ciphertext,tag,nonce} envelope (hex or Base64, auto-detected).
 * `out` receives the plaintext (NUL-terminated when room allows) and must
 * hold at least the ciphertext byte length. Returns THALOVANT_ERR_AUTH on
 * tag mismatch.
 */
int thalovant_envelope_decrypt_json(const uint8_t key[16], const char *envelope_json, size_t len,
                                    uint8_t *out, size_t cap, size_t *out_len);

/* Binary envelope: out = nonce(16) || ciphertext || tag(16). */
int thalovant_envelope_encrypt_binary(const uint8_t key[16],
                                      const uint8_t nonce[THALOVANT_GCM_NONCE_LEN],
                                      const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                                      size_t cap, size_t *out_len);

int thalovant_envelope_decrypt_binary(const uint8_t key[16], const uint8_t *in, size_t len,
                                      uint8_t *out, size_t cap, size_t *out_len);

/* ------------------------------------------------------------- handshake */

/*
 * The plaintext hello frame:
 *   {"msg_type":"hello","payload":{"pubkey":"<pubkey>","session":
 *   {"session_id":"<session_id>"},"site_id":"<site_id>"},...}
 * `pubkey` may be NULL (serializes as ""). Returns the length written.
 */
int thalovant_wire_hello(const char *pubkey, const char *session_id, const char *site_id,
                         char *out, size_t cap);

/* base64("<user_agent>:<access_key>") — the authorization query value. */
int thalovant_wire_authorization(const char *user_agent, const char *access_key, char *out,
                                 size_t cap);

/*
 * Append "?authorization=<percent-encoded authorization>" (or "&..." when
 * the endpoint already has a query string). Returns the length written.
 */
int thalovant_wire_ws_url(const char *endpoint, const char *authorization, char *out, size_t cap);

/* ---------------------------------------------------------- binary frame */

/*
 * HiveMind binary frame (hivemind-bus-client compatible):
 *   0x80 | (type_id << 1), metadata_len(1), metadata JSON, payload JSON.
 * Metadata is limited to 255 bytes. Unknown msg_type maps to "3rdparty".
 */
int thalovant_wire_encode_binary(const thalovant_hive_message *msg, uint8_t *out, size_t cap,
                                 size_t *out_len);

typedef struct {
    const char *msg_type; /* static string, e.g. "hello" */
    int type_id;
    size_t metadata_len; /* bytes written to the metadata buffer */
    size_t payload_len;  /* bytes written to the payload buffer */
} thalovant_wire_binary_frame;

/*
 * Decode a binary frame into caller-provided metadata/payload buffers
 * (NUL-terminated JSON text). Compressed frames and protocol versions > 1
 * yield THALOVANT_ERR_UNSUPPORTED.
 */
int thalovant_wire_decode_binary(const uint8_t *frame, size_t len, char *metadata,
                                 size_t metadata_cap, char *payload, size_t payload_cap,
                                 thalovant_wire_binary_frame *out);

/* msg_type <-> integer id mapping used by the binary frame. */
int thalovant_wire_msg_type_id(const char *msg_type); /* -1 when unknown */
const char *thalovant_wire_msg_type_name(int id);     /* NULL when unknown */

#endif /* THALOVANT_WIRE_H */
