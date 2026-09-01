#include "thalovant/ask.h"

#include <ctype.h>
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

static int append_json_string(char *out, size_t cap, size_t *pos, const char *text)
{
    int rc = append(out, cap, pos, "\"");
    if (rc != THALOVANT_OK) {
        return rc;
    }
    for (const char *c = text; *c != '\0'; c++) {
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

int thalovant_ask_build_payload(const thalovant_ask_request *request, char *out, size_t cap)
{
    if (request == NULL || out == NULL || request->text == NULL || request->text[0] == '\0' ||
        request->session_id == NULL || request->request_id == NULL) {
        return THALOVANT_ERR_INVALID;
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
    if ((rc = append(out, cap, &pos, "}}}")) != THALOVANT_OK) return rc;
    return (int)pos;
}

int thalovant_ask_build_frame(const thalovant_ask_request *request, char *out, size_t cap)
{
    char payload[THALOVANT_ASK_TEXT_MAX + 512];
    int rc = thalovant_ask_build_payload(request, payload, sizeof(payload));
    if (rc < 0) {
        return rc;
    }
    thalovant_hive_message msg = { "bus", payload, NULL, NULL, NULL, NULL, NULL, NULL };
    return thalovant_wire_serialize(&msg, out, cap);
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
