#include "thalovant/ask.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "thalovant/json.h"
#include "thalovant/wire.h"

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

static int append_json_range(char *out, size_t cap, size_t *pos, const char *text, size_t length)
{
    int rc = append(out, cap, pos, "\"");
    if (rc != THALOVANT_OK) {
        return rc;
    }
    for (const char *c = text; (size_t)(c - text) < length; c++) {
        unsigned char ch = (unsigned char)*c;
        char buf[8] = { 0 };
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
                buf[0] = '\\';
                buf[1] = 'u';
                buf[2] = '0';
                buf[3] = '0';
                buf[4] = "0123456789abcdef"[ch >> 4];
                buf[5] = "0123456789abcdef"[ch & 0x0f];
            } else {
                buf[0] = (char)ch;
            }
            break;
        }
        rc = append(out, cap, pos, piece);
        if (rc != THALOVANT_OK) {
            return rc;
        }
    }
    return append(out, cap, pos, "\"");
}

static int append_json_string(char *out, size_t cap, size_t *pos, const char *text)
{
    return append_json_range(out, cap, pos, text, strlen(text));
}

static size_t trimmed(const char **text)
{
    while (**text != '\0' && isspace((unsigned char)**text)) (*text)++;
    size_t size = strlen(*text);
    while (size > 0 && isspace((unsigned char)(*text)[size - 1])) size--;
    return size;
}

static int append_pipeline(char *out, size_t cap, size_t *pos, const char *json)
{
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    int count = thalovant_json_parse(json, strlen(json), tokens, THALOVANT_WIRE_MAX_TOKENS);
    if (count < 0) return count;
    bool started = false;
    for (int i = 1; i < count; i++) {
        if (tokens[i].type != THALOVANT_JSON_STRING) return THALOVANT_ERR_INVALID;
        char stage[THALOVANT_ASK_TEXT_MAX];
        int rc = thalovant_json_unescape(json, &tokens[i], stage, sizeof(stage));
        if (rc < 0) return rc;
        const char *value = stage;
        size_t size = (size_t)rc;
        while (size > 0 && isspace((unsigned char)*value)) { value++; size--; }
        while (size > 0 && isspace((unsigned char)value[size - 1])) size--;
        if (size == 0) continue;
        rc = append(out, cap, pos, started ? "," : ",\"pipeline\":[");
        if (rc < 0) return rc;
        if ((rc = append_json_range(out, cap, pos, value, size)) < 0) return rc;
        started = true;
    }
    return started ? append(out, cap, pos, "]") : THALOVANT_OK;
}

int thalovant_ask_build_payload(const thalovant_ask_request *request, char *out, size_t cap)
{
    return thalovant_ask_build_payload_with_hints(request, NULL, out, cap);
}

static int validate_hint(const char *json, thalovant_json_type type)
{
    if (json == NULL) return THALOVANT_OK;
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    int count = thalovant_json_parse(json, strlen(json), tokens, THALOVANT_WIRE_MAX_TOKENS);
    if (count < 0) return count;
    return count > 0 && tokens[0].type == type ? THALOVANT_OK : THALOVANT_ERR_INVALID;
}

int thalovant_ask_build_payload_with_hints(const thalovant_ask_request *request,
    const thalovant_ask_hints *hints, char *out, size_t cap)
{
    if (request == NULL || out == NULL || request->text == NULL || request->text[0] == '\0' ||
        request->session_id == NULL || request->request_id == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    if (hints != NULL) {
        int result = validate_hint(hints->pipeline_json, THALOVANT_JSON_ARRAY);
        if (result < 0) return result;
        result = validate_hint(hints->location_json, THALOVANT_JSON_OBJECT);
        if (result < 0) return result;
    }
    const char *lang = request->lang != NULL ? request->lang : "en-us";
    size_t pos = 0;
    int rc;
    if ((rc = append(out, cap, &pos,
                     "{\"type\":\"recognizer_loop:utterance\",\"data\":{\"utterances\":[")) !=
        THALOVANT_OK)
        return rc;
    if ((rc = append_json_string(out, cap, &pos, request->text)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, "],\"lang\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, lang)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, "},\"context\":{\"request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, request->request_id)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"thalovant_request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, request->request_id)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"session\":{\"session_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, request->session_id)) != THALOVANT_OK) return rc;
    if (request->site_id != NULL) {
        if ((rc = append(out, cap, &pos, ",\"site_id\":")) != THALOVANT_OK) return rc;
        if ((rc = append_json_string(out, cap, &pos, request->site_id)) != THALOVANT_OK) return rc;
    }
    if ((rc = append(out, cap, &pos, ",\"lang\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, lang)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, request->request_id)) != THALOVANT_OK) return rc;
    if (hints != NULL && hints->pipeline_json != NULL) {
        if ((rc = append_pipeline(out, cap, &pos, hints->pipeline_json)) < 0) return rc;
    }
    if ((rc = append(out, cap, &pos, "}")) != THALOVANT_OK) return rc;
    if (hints != NULL && hints->stt_lang != NULL) {
        const char *hint = hints->stt_lang;
        size_t size = trimmed(&hint);
        if (size > 0) {
            if ((rc = append(out, cap, &pos, ",\"stt_lang\":")) < 0) return rc;
            if ((rc = append_json_range(out, cap, &pos, hint, size)) < 0) return rc;
        }
    }
    if (hints != NULL && hints->location_json != NULL && strcmp(hints->location_json, "{}") != 0) {
        if ((rc = append(out, cap, &pos, ",\"location\":")) != THALOVANT_OK) return rc;
        if ((rc = append(out, cap, &pos, hints->location_json)) != THALOVANT_OK) return rc;
    }
    if ((rc = append(out, cap, &pos, "}}")) != THALOVANT_OK) return rc;
    return (int)pos;
}

int thalovant_ask_build_frame(const thalovant_ask_request *request, char *out, size_t cap)
{
    return thalovant_ask_build_frame_with_hints(request, NULL, out, cap);
}

int thalovant_ask_build_frame_with_hints(const thalovant_ask_request *request,
    const thalovant_ask_hints *hints, char *out, size_t cap)
{
    if (out == NULL || cap == 0) return THALOVANT_ERR_INVALID;
    /* Serialize the default bus envelope directly around the caller-buffer
     * payload. Existing Node wire fixtures cover the field order and defaults. */
    size_t pos = 0;
    int rc = append(out, cap, &pos, "{\"msg_type\":\"bus\",\"payload\":");
    if (rc < 0) return rc;
    rc = thalovant_ask_build_payload_with_hints(request, hints, out + pos, cap - pos);
    if (rc < 0) return rc;
    pos += (size_t)rc;
    rc = append(out, cap, &pos, ",\"metadata\":{},\"route\":[],\"node\":null,\"target_site_id\":null,\"target_pubkey\":null,\"source_peer\":null}");
    return rc < 0 ? rc : (int)pos;
}

/* request_id ?? thalovant_request_id ?? correlation_id from one object. */
static int request_id_from(const char *js, const thalovant_json_tok *toks, int count, int obj,
                           char *out, size_t cap)
{
    static const char *const KEYS[] = { "request_id", "thalovant_request_id", "correlation_id" };
    int value = thalovant_json_object_get_alias(js, toks, count, obj, KEYS, 3);
    if (value < 0) {
        return THALOVANT_ERR_MISSING;
    }
    int len = thalovant_json_as_string(js, &toks[value], out, cap);
    if (len < 0) {
        return len;
    }
    return len > 0 ? THALOVANT_OK : THALOVANT_ERR_MISSING;
}

static int extract_text(const char *js, const thalovant_json_tok *toks, int count, int data,
                        char *out, size_t cap)
{
    out[0] = '\0';
    if (data < 0 || toks[data].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_OK;
    }
    int value = thalovant_json_object_get(js, toks, count, data, "utterance");
    if (value >= 0 && toks[value].type == THALOVANT_JSON_STRING) {
        return thalovant_json_unescape(js, &toks[value], out, cap) < 0 ? THALOVANT_ERR_NOMEM
                                                                       : THALOVANT_OK;
    }
    value = thalovant_json_object_get(js, toks, count, data, "text");
    if (value >= 0 && toks[value].type == THALOVANT_JSON_STRING) {
        return thalovant_json_unescape(js, &toks[value], out, cap) < 0 ? THALOVANT_ERR_NOMEM
                                                                       : THALOVANT_OK;
    }
    value = thalovant_json_object_get(js, toks, count, data, "utterances");
    if (value >= 0) {
        if (toks[value].type == THALOVANT_JSON_STRING) {
            return thalovant_json_unescape(js, &toks[value], out, cap) < 0 ? THALOVANT_ERR_NOMEM
                                                                           : THALOVANT_OK;
        }
        if (toks[value].type == THALOVANT_JSON_ARRAY && toks[value].size > 0 &&
            value + 1 < count && toks[value + 1].type == THALOVANT_JSON_STRING) {
            return thalovant_json_unescape(js, &toks[value + 1], out, cap) < 0
                       ? THALOVANT_ERR_NOMEM
                       : THALOVANT_OK;
        }
    }
    return THALOVANT_OK;
}

int thalovant_ask_classify(const char *frame_json, size_t len, const char *request_id,
                           thalovant_ask_event *out)
{
    if (frame_json == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->kind = THALOVANT_ASK_IGNORE;

    thalovant_json_tok toks[THALOVANT_WIRE_MAX_TOKENS];
    int count = thalovant_json_parse(frame_json, len, toks, THALOVANT_WIRE_MAX_TOKENS);
    if (count < 0) {
        return count;
    }
    if (toks[0].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }
    int msg_type = thalovant_json_object_get(frame_json, toks, count, 0, "msg_type");
    if (msg_type < 0 || !thalovant_json_str_eq(frame_json, &toks[msg_type], "bus")) {
        return THALOVANT_OK;
    }
    int payload = thalovant_json_object_get(frame_json, toks, count, 0, "payload");
    if (payload < 0 || toks[payload].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_OK;
    }
    int type = thalovant_json_object_get(frame_json, toks, count, payload, "type");
    if (type < 0 || toks[type].type != THALOVANT_JSON_STRING) {
        return THALOVANT_OK;
    }

    thalovant_ask_kind kind = THALOVANT_ASK_IGNORE;
    if (thalovant_json_str_eq(frame_json, &toks[type], "speak") ||
        thalovant_json_str_eq(frame_json, &toks[type], "ovos.utterance.speak")) {
        kind = THALOVANT_ASK_SPEAK;
    } else if (thalovant_json_str_eq(frame_json, &toks[type], "mycroft.audio.queue")) {
        kind = THALOVANT_ASK_AUDIO;
    } else if (thalovant_json_str_eq(frame_json, &toks[type], "ovos.utterance.handled")) {
        kind = THALOVANT_ASK_HANDLED;
    } else if (thalovant_json_str_eq(frame_json, &toks[type], "complete_intent_failure") ||
               thalovant_json_str_eq(frame_json, &toks[type], "ovos.intent.unmatched")) {
        /* Legacy Mycroft name ("complete_intent_failure") vs current OVOS
         * name ("ovos.intent.unmatched"); both mean the utterance matched no
         * intent. Keep the legacy name for older runtimes. */
        kind = THALOVANT_ASK_INTENT_FAILURE;
    } else if (thalovant_json_str_eq(frame_json, &toks[type], "hive.policy.denied")) {
        kind = THALOVANT_ASK_POLICY_DENIED;
    } else if (thalovant_json_str_eq(frame_json, &toks[type], "hive.query.timeout")) {
        kind = THALOVANT_ASK_QUERY_TIMEOUT;
    } else {
        return THALOVANT_OK;
    }

    int data = thalovant_json_object_get(frame_json, toks, count, payload, "data");
    int context = thalovant_json_object_get(frame_json, toks, count, payload, "context");

    /* Event request id: context ?? context.session ?? data (Node order). */
    int rc = THALOVANT_ERR_MISSING;
    if (context >= 0 && toks[context].type == THALOVANT_JSON_OBJECT) {
        rc = request_id_from(frame_json, toks, count, context, out->request_id,
                             sizeof(out->request_id));
        if (rc == THALOVANT_ERR_MISSING) {
            int session = thalovant_json_object_get(frame_json, toks, count, context, "session");
            if (session >= 0 && toks[session].type == THALOVANT_JSON_OBJECT) {
                rc = request_id_from(frame_json, toks, count, session, out->request_id,
                                     sizeof(out->request_id));
            }
        }
    }
    if (rc == THALOVANT_ERR_MISSING && data >= 0 && toks[data].type == THALOVANT_JSON_OBJECT) {
        rc = request_id_from(frame_json, toks, count, data, out->request_id,
                             sizeof(out->request_id));
    }
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) {
        return rc;
    }
    bool has_request = rc == THALOVANT_OK;

    if (request_id != NULL) {
        /* Required correlation: no id, or a different id, is a miss. */
        if (!has_request || strcmp(out->request_id, request_id) != 0) {
            out->request_id[0] = '\0';
            return THALOVANT_OK;
        }
    }

    rc = extract_text(frame_json, toks, count, data, out->text, sizeof(out->text));
    if (rc != THALOVANT_OK) {
        return rc;
    }
    out->kind = kind;
    out->is_failure = kind == THALOVANT_ASK_INTENT_FAILURE ||
                      kind == THALOVANT_ASK_POLICY_DENIED || kind == THALOVANT_ASK_QUERY_TIMEOUT;
    return THALOVANT_OK;
}

int thalovant_ask_normalize_text(char *text)
{
    if (text == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    size_t out = 0;
    bool pending_space = false;
    for (const char *c = text; *c != '\0'; c++) {
        if (isspace((unsigned char)*c)) {
            if (out > 0) {
                pending_space = true;
            }
            continue;
        }
        if (pending_space) {
            text[out++] = ' ';
            pending_space = false;
        }
        text[out++] = *c;
    }
    text[out] = '\0';
    return (int)out;
}


static int event_tokens(const char *frame, size_t len, const char *request_id,
    thalovant_json_tok *tokens, thalovant_ask_kind *kind)
{
    thalovant_ask_event event;
    int rc = thalovant_ask_classify(frame, len, request_id, &event);
    if (rc < 0) return rc;
    if (event.kind == THALOVANT_ASK_IGNORE) return THALOVANT_ERR_MISSING;
    *kind = event.kind;
    return thalovant_json_parse(frame, len, tokens, THALOVANT_WIRE_MAX_TOKENS);
}

int thalovant_ask_audio_decode(const char *frame, size_t len, const char *request_id,
    char *scratch, size_t scratch_cap, uint8_t *out, size_t out_cap)
{
    if (frame == NULL || scratch == NULL || out == NULL) return THALOVANT_ERR_INVALID;
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    thalovant_ask_kind kind;
    int count = event_tokens(frame, len, request_id, tokens, &kind);
    if (count < 0) return count;
    if (kind != THALOVANT_ASK_AUDIO) return THALOVANT_ERR_MISSING;
    int payload = thalovant_json_object_get(frame, tokens, count, 0, "payload");
    int data = thalovant_json_object_get(frame, tokens, count, payload, "data");
    int binary = thalovant_json_object_get(frame, tokens, count, data, "binary_data");
    if (binary < 0 || tokens[binary].type != THALOVANT_JSON_STRING) return THALOVANT_ERR_MISSING;
    /* JSON escapes can use six source bytes per decoded ASCII character. */
    if ((size_t)(tokens[binary].end - tokens[binary].start) > THALOVANT_AUDIO_CLIP_MAX * 12u) return THALOVANT_ERR_NOMEM;
    int encoded = thalovant_json_unescape(frame, &tokens[binary], scratch, scratch_cap);
    if (encoded < 0) return encoded;
    if (encoded == 0) return THALOVANT_ERR_MISSING;
    if ((size_t)encoded > THALOVANT_AUDIO_CLIP_MAX * 2u) return THALOVANT_ERR_NOMEM;
    size_t written = 0;
    int high = -1;
    for (int i = 0; i < encoded; i++) {
        unsigned char c = (unsigned char)scratch[i];
        if (c == 32 || (c >= 9 && c <= 13)) {
            if (high >= 0) return THALOVANT_ERR_INVALID;
            continue;
        }
        int value = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (value < 0) return THALOVANT_ERR_INVALID;
        if (high < 0) high = value;
        else {
            if (written >= out_cap) return THALOVANT_ERR_NOMEM;
            out[written++] = (uint8_t)(high * 16 + value); high = -1;
        }
    }
    return high >= 0 ? THALOVANT_ERR_INVALID : (int)written;
}

int thalovant_ask_event_language(const char *frame, size_t len, const char *request_id, char *out, size_t cap)
{
    if (frame == NULL || out == NULL || cap == 0) return THALOVANT_ERR_INVALID;
    out[0] = '\0';
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    thalovant_ask_kind kind;
    int count = event_tokens(frame, len, request_id, tokens, &kind);
    if (count < 0) return count;
    int payload = thalovant_json_object_get(frame, tokens, count, 0, "payload");
    int data = thalovant_json_object_get(frame, tokens, count, payload, "data");
    int context = thalovant_json_object_get(frame, tokens, count, payload, "context");
    int session = thalovant_json_object_get(frame, tokens, count, context, "session");
    const int objects[] = { data, context, session };
    for (size_t i = 0; i < 3; i++) {
        int value = thalovant_json_object_get(frame, tokens, count, objects[i], "lang");
        if (value >= 0 && tokens[value].type == THALOVANT_JSON_STRING) {
            int rc = thalovant_json_unescape(frame, &tokens[value], out, cap);
            if (rc != 0) return rc;
        }
    }
    return 0;
}

bool thalovant_audio_budget_accept(thalovant_audio_budget *budget, size_t encoded_chars)
{
    if (budget == NULL) return false;
    if (encoded_chars > THALOVANT_AUDIO_CLIP_MAX * 2u ||
        budget->encoded_chars > THALOVANT_REPLY_MEDIA_MAX * 2u ||
        encoded_chars > THALOVANT_REPLY_MEDIA_MAX * 2u - budget->encoded_chars) {
        if (budget->dropped < SIZE_MAX) budget->dropped++;
        return false;
    }
    budget->encoded_chars += encoded_chars;
    return true;
}

static int event_context_identifier(const char *frame, size_t len, const char *request_id,
    const char *field, char *out, size_t cap)
{
    if (frame == NULL || out == NULL || cap == 0) return THALOVANT_ERR_INVALID;
    out[0] = '\0';
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    thalovant_ask_kind kind;
    int count = event_tokens(frame, len, request_id, tokens, &kind);
    if (count < 0) return count;
    int payload = thalovant_json_object_get(frame, tokens, count, 0, "payload");
    int context = thalovant_json_object_get(frame, tokens, count, payload, "context");
    int value = thalovant_json_object_get(frame, tokens, count, context, field);
    if (value < 0 || tokens[value].type != THALOVANT_JSON_STRING) return THALOVANT_ERR_MISSING;
    int length = thalovant_json_unescape(frame, &tokens[value], out, cap);
    if (length < 0) { out[0] = '\0'; return length; }
    if (length == 0) return THALOVANT_ERR_MISSING;
    if (strlen(out) != (size_t)length) { out[0] = '\0'; return THALOVANT_ERR_INVALID; }
    return length;
}

int thalovant_ask_event_pipeline_id(const char *frame, size_t len, const char *request_id,
    char *out, size_t cap)
{
    return event_context_identifier(frame, len, request_id, "pipeline_id", out, cap);
}

int thalovant_ask_event_skill_id(const char *frame, size_t len, const char *request_id,
    char *out, size_t cap)
{
    return event_context_identifier(frame, len, request_id, "skill_id", out, cap);
}

/* A whole, non-negative count from a JSON number or numeric string, or 0. */
static long refusal_count(const char *frame, const thalovant_json_tok *tokens, int count, int object,
    const char *field)
{
    int value = thalovant_json_object_get(frame, tokens, count, object, field);
    if (value < 0) return 0;
    if (tokens[value].type != THALOVANT_JSON_PRIMITIVE && tokens[value].type != THALOVANT_JSON_STRING) return 0;
    char text[32];
    size_t length = (size_t)(tokens[value].end - tokens[value].start);
    if (length == 0 || length >= sizeof(text)) return 0;
    memcpy(text, frame + tokens[value].start, length);
    text[length] = '\0';
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    /* ERANGE too: a number past LONG_MAX fits this buffer and comes back
     * clamped, which would report a limit the hub never sent. */
    if (end == text || *end != '\0' || errno == ERANGE || parsed < 0) return 0;
    return parsed;
}

/* Copy a bounded string field, or leave it empty. ERR_NOMEM when it will not fit. */
static int refusal_string(const char *frame, const thalovant_json_tok *tokens, int count, int object,
    const char *field, char *out, size_t cap)
{
    out[0] = '\0';
    int value = thalovant_json_object_get(frame, tokens, count, object, field);
    if (value < 0 || tokens[value].type != THALOVANT_JSON_STRING) return 0;
    int length = thalovant_json_unescape(frame, &tokens[value], out, cap);
    if (length < 0) { out[0] = '\0'; return length; }
    return length;
}

int thalovant_ask_refusal(const char *frame, size_t len, const char *request_id,
    thalovant_refusal *out)
{
    if (frame == NULL || out == NULL) return THALOVANT_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    thalovant_ask_kind kind;
    int count = event_tokens(frame, len, request_id, tokens, &kind);
    if (count < 0) return count;
    if (kind != THALOVANT_ASK_POLICY_DENIED) return THALOVANT_ERR_MISSING;
    int payload = thalovant_json_object_get(frame, tokens, count, 0, "payload");
    int data = thalovant_json_object_get(frame, tokens, count, payload, "data");
    if (data < 0) return THALOVANT_ERR_MISSING;
    int rc = refusal_string(frame, tokens, count, data, "denied_type", out->denied_type, sizeof(out->denied_type));
    if (rc < 0) return rc;
    rc = refusal_string(frame, tokens, count, data, "code", out->code, sizeof(out->code));
    if (rc < 0) return rc;
    rc = refusal_string(frame, tokens, count, data, "reason", out->reason, sizeof(out->reason));
    if (rc < 0) return rc;
    if (strcmp(out->code, THALOVANT_POLICY_QUOTA_EXCEEDED) == 0) {
        out->has_quota = true;
        /* The policy's own detail rides nested under data.data. */
        int inner = thalovant_json_object_get(frame, tokens, count, data, "data");
        if (inner >= 0) {
            rc = refusal_string(frame, tokens, count, inner, "period", out->quota_period, sizeof(out->quota_period));
            if (rc < 0) return rc;
            out->quota_limit = refusal_count(frame, tokens, count, inner, "limit");
            out->quota_used = refusal_count(frame, tokens, count, inner, "used");
            out->quota_reset_after = refusal_count(frame, tokens, count, inner, "reset_after");
        }
    }
    return 0;
}

int thalovant_ask_refusal_allowed(const char *frame, size_t len, size_t index, char *out, size_t cap)
{
    if (frame == NULL || out == NULL || cap == 0) return THALOVANT_ERR_INVALID;
    out[0] = '\0';
    thalovant_json_tok tokens[THALOVANT_WIRE_MAX_TOKENS];
    thalovant_ask_kind kind;
    int count = event_tokens(frame, len, NULL, tokens, &kind);
    if (count < 0) return count;
    if (kind != THALOVANT_ASK_POLICY_DENIED) return THALOVANT_ERR_MISSING;
    int payload = thalovant_json_object_get(frame, tokens, count, 0, "payload");
    int data = thalovant_json_object_get(frame, tokens, count, payload, "data");
    int inner = thalovant_json_object_get(frame, tokens, count, data, "data");
    int allowed = thalovant_json_object_get(frame, tokens, count, inner, "allowed");
    if (allowed < 0 || tokens[allowed].type != THALOVANT_JSON_ARRAY) return THALOVANT_ERR_MISSING;
    size_t seen = 0;
    int stop = thalovant_json_skip(tokens, count, allowed);
    if (stop < 0) return stop;
    for (int token = allowed + 1; token > 0 && token < stop;
         token = thalovant_json_skip(tokens, count, token)) {
        /* Non-empty strings only, trimmed: a number, a null or a blank in the
         * hub's list is not a message type an operator can allow. */
        if (tokens[token].type != THALOVANT_JSON_STRING) continue;
        char scratch[THALOVANT_ASK_TEXT_MAX];
        int length = thalovant_json_unescape(frame, &tokens[token], scratch, sizeof(scratch));
        if (length < 0) return length;
        char *start = scratch;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
        char *end = start + strlen(start);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) --end;
        *end = '\0';
        if (*start == '\0') continue;
        if (seen++ != index) continue;
        size_t size = strlen(start);
        if (size + 1 > cap) return THALOVANT_ERR_NOMEM;
        memcpy(out, start, size + 1);
        return (int)size;
    }
    return THALOVANT_ERR_MISSING;
}

bool thalovant_refusal_belongs_to_ask(const char *request_id, const char *own_request_id,
    const char *denied_type, size_t asks_in_flight, size_t queries_in_flight,
    size_t sends_in_flight)
{
    if (request_id != NULL && request_id[0] != '\0') {
        return own_request_id != NULL && strcmp(request_id, own_request_id) == 0;
    }
    return denied_type != NULL
        && strcmp(denied_type, "recognizer_loop:utterance") == 0
        && asks_in_flight == 1 && queries_in_flight == 0 && sends_in_flight == 0;
}

bool thalovant_reply_claimed(bool handled, bool has_failure,
    const char *const *pipeline_ids, size_t pipeline_count)
{
    bool stamped = false;
    if (!handled || has_failure || (pipeline_ids == NULL && pipeline_count != 0)) return false;
    for (size_t i = 0; i < pipeline_count; ++i) {
        const char *stage = pipeline_ids[i];
        if (stage == NULL || stage[0] == '\0') continue;
        stamped = true;
        if (strstr(stage, "fallback") == NULL) return true;
    }
    return !stamped;
}
