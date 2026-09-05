#include "thalovant/intents.h"

#include <ctype.h>
#include <string.h>

#include "thalovant/json.h"
#include "thalovant/wire.h"

/* Sized like the ask payload buffer: ids, a skill and intent name, three
 * copies of the request id and the JSON around them. */
#define TLV_INTENT_PAYLOAD_CAP 1024

#if THALOVANT_INTENT_SESSION_ID_MAX < 8
#error "THALOVANT_INTENT_SESSION_ID_MAX must hold the \"default\" session id"
#endif

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

/* ------------------------------------------------------------- queries */

/*
 * The context every inventory query carries: the request id where ask()
 * puts it, the language beside it as the reference SDK sends it (the hub
 * echoes both), and the session the reply is answered over.
 */
static int append_context(char *out, size_t cap, size_t *pos, const char *lang,
                          const char *session_id, const char *site_id, const char *request_id)
{
    int rc;
    if ((rc = append(out, cap, pos, ",\"context\":{\"request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, request_id)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, pos, ",\"thalovant_request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, request_id)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, pos, ",\"lang\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, lang)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, pos, ",\"session\":{\"session_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, session_id)) != THALOVANT_OK) return rc;
    if (site_id != NULL) {
        if ((rc = append(out, cap, pos, ",\"site_id\":")) != THALOVANT_OK) return rc;
        if ((rc = append_json_string(out, cap, pos, site_id)) != THALOVANT_OK) return rc;
    }
    if ((rc = append(out, cap, pos, ",\"lang\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, lang)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, pos, ",\"request_id\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, pos, request_id)) != THALOVANT_OK) return rc;
    return append(out, cap, pos, "}}}");
}

int thalovant_intent_list_build_payload(const thalovant_intent_list_request *request, char *out,
                                        size_t cap)
{
    if (request == NULL || out == NULL || request->session_id == NULL ||
        request->request_id == NULL || request->request_id[0] == '\0') {
        return THALOVANT_ERR_INVALID;
    }
    const char *lang = request->lang != NULL ? request->lang : "en-us";
    size_t pos = 0;
    int rc;
    if ((rc = append(out, cap, &pos, "{\"type\":\"" THALOVANT_EVENT_INTENT_LIST
                                     "\",\"data\":{\"lang\":")) != THALOVANT_OK)
        return rc;
    if ((rc = append_json_string(out, cap, &pos, lang)) != THALOVANT_OK) return rc;
    if (request->include_definitions) {
        if ((rc = append(out, cap, &pos, ",\"include_definitions\":true")) != THALOVANT_OK)
            return rc;
    }
    if ((rc = append(out, cap, &pos, "}")) != THALOVANT_OK) return rc;
    if ((rc = append_context(out, cap, &pos, lang, request->session_id, request->site_id,
                             request->request_id)) != THALOVANT_OK)
        return rc;
    return (int)pos;
}

int thalovant_intent_list_build_frame(const thalovant_intent_list_request *request, char *out,
                                      size_t cap)
{
    char payload[TLV_INTENT_PAYLOAD_CAP];
    int rc = thalovant_intent_list_build_payload(request, payload, sizeof(payload));
    if (rc < 0) {
        return rc;
    }
    thalovant_hive_message msg = { "bus", payload, NULL, NULL, NULL, NULL, NULL, NULL };
    return thalovant_wire_serialize(&msg, out, cap);
}

int thalovant_intent_describe_build_payload(const thalovant_intent_describe_request *request,
                                            char *out, size_t cap)
{
    if (request == NULL || out == NULL || request->skill_id == NULL ||
        request->skill_id[0] == '\0' || request->intent_name == NULL ||
        request->intent_name[0] == '\0' || request->session_id == NULL ||
        request->request_id == NULL || request->request_id[0] == '\0') {
        return THALOVANT_ERR_INVALID;
    }
    const char *lang = request->lang != NULL ? request->lang : "en-us";
    size_t pos = 0;
    int rc;
    if ((rc = append(out, cap, &pos, "{\"type\":\"" THALOVANT_EVENT_INTENT_DESCRIBE
                                     "\",\"data\":{\"skill_id\":")) != THALOVANT_OK)
        return rc;
    if ((rc = append_json_string(out, cap, &pos, request->skill_id)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"intent_name\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, request->intent_name)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, ",\"lang\":")) != THALOVANT_OK) return rc;
    if ((rc = append_json_string(out, cap, &pos, lang)) != THALOVANT_OK) return rc;
    if ((rc = append(out, cap, &pos, "}")) != THALOVANT_OK) return rc;
    if ((rc = append_context(out, cap, &pos, lang, request->session_id, request->site_id,
                             request->request_id)) != THALOVANT_OK)
        return rc;
    return (int)pos;
}

int thalovant_intent_describe_build_frame(const thalovant_intent_describe_request *request,
                                          char *out, size_t cap)
{
    char payload[TLV_INTENT_PAYLOAD_CAP];
    int rc = thalovant_intent_describe_build_payload(request, payload, sizeof(payload));
    if (rc < 0) {
        return rc;
    }
    thalovant_hive_message msg = { "bus", payload, NULL, NULL, NULL, NULL, NULL, NULL };
    return thalovant_wire_serialize(&msg, out, cap);
}

/* ------------------------------------------------------- field readers */

/*
 * Copy the string value of `key` in `obj` into `out`, coerced the way the
 * reference SDKs do (null and absence read as ""). THALOVANT_OK with a
 * non-empty value, THALOVANT_ERR_MISSING when absent, null, or empty, and
 * the JSON/NOMEM errors otherwise.
 */
static int read_text(const char *js, const thalovant_json_tok *obj, const char *key, char *out,
                     size_t cap)
{
    out[0] = '\0';
    thalovant_json_tok value;
    int rc = thalovant_json_scan_key(js, obj, key, &value);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    if (value.type != THALOVANT_JSON_STRING && value.type != THALOVANT_JSON_PRIMITIVE) {
        return THALOVANT_ERR_MISSING;
    }
    int len = thalovant_json_as_string(js, &value, out, cap);
    if (len < 0) {
        return len;
    }
    return len > 0 ? THALOVANT_OK : THALOVANT_ERR_MISSING;
}

/* read_text, but absence is not an error: only JSON/NOMEM failures return. */
static int read_optional_text(const char *js, const thalovant_json_tok *obj, const char *key,
                              char *out, size_t cap)
{
    int rc = read_text(js, obj, key, out, cap);
    return rc == THALOVANT_ERR_MISSING ? THALOVANT_OK : rc;
}

/* The value of `key` when it is an object or array; false when absent. */
static bool read_container(const char *js, const thalovant_json_tok *obj, const char *key,
                           thalovant_json_type type, thalovant_json_tok *value)
{
    return thalovant_json_scan_key(js, obj, key, value) == THALOVANT_OK && value->type == type;
}

/* request_id ?? thalovant_request_id ?? correlation_id from one object. */
static int request_id_from(const char *js, const thalovant_json_tok *obj, char *out, size_t cap)
{
    static const char *const KEYS[] = { "request_id", "thalovant_request_id", "correlation_id" };
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        int rc = read_text(js, obj, KEYS[i], out, cap);
        if (rc != THALOVANT_ERR_MISSING) {
            return rc;
        }
    }
    return THALOVANT_ERR_MISSING;
}

static thalovant_intent_engine engine_of(const char *method)
{
    if (strcmp(method, "template") == 0) {
        return THALOVANT_INTENT_ENGINE_PADATIOUS;
    }
    if (strcmp(method, "keyword") == 0) {
        return THALOVANT_INTENT_ENGINE_ADAPT;
    }
    return THALOVANT_INTENT_ENGINE_UNKNOWN;
}

/* --------------------------------------------------------- allow-list */

/*
 * Walk a denial's `allowed` array, delivering its string entries. A number
 * or a null in the list is not a message type: naming one would send an
 * operator reading which types to allow after "3" or "null", so they are
 * passed over. `fn` NULL counts them without copying anything out.
 * Returns the number of string entries, or a negative error.
 */
static int walk_allowed(const char *js, size_t len, int count, thalovant_intent_allowed_fn fn,
                        void *user)
{
    thalovant_json_tok array = { THALOVANT_JSON_ARRAY, 0, (int)len, count, -1 };
    size_t cursor = 0;
    thalovant_json_tok item;
    int delivered = 0;
    int rc;
    while ((rc = thalovant_json_scan_next(js, &array, &cursor, &item)) == 1) {
        if (item.type != THALOVANT_JSON_STRING) {
            continue;
        }
        if (fn == NULL) {
            delivered++;
            continue;
        }
        thalovant_intent_allowed_type allowed;
        int n = thalovant_json_as_string(js, &item, allowed.type, sizeof(allowed.type));
        if (n < 0) {
            return n;
        }
        allowed.index = delivered;
        delivered++;
        if (!fn(&allowed, user)) {
            return delivered;
        }
    }
    return rc < 0 ? rc : delivered;
}

int thalovant_intent_allowed_types(const thalovant_intent_event *event,
                                   thalovant_intent_allowed_fn fn, void *user)
{
    if (event == NULL || fn == NULL || event->kind != THALOVANT_INTENT_POLICY_DENIED) {
        return THALOVANT_ERR_INVALID;
    }
    if (event->allowed_json == NULL || event->allowed_len == 0) {
        return 0;
    }
    return walk_allowed(event->allowed_json, event->allowed_len, event->allowed_count, fn, user);
}

/* ------------------------------------------------------------ classify */

int thalovant_intent_classify(const char *frame_json, size_t len, const char *request_id,
                              thalovant_intent_event *out)
{
    if (frame_json == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->kind = THALOVANT_INTENT_IGNORE;
    out->ok = true;

    thalovant_json_tok root;
    int next = thalovant_json_scan(frame_json, len, 0, &root);
    if (next < 0) {
        return next;
    }
    if (root.type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }
    for (size_t i = (size_t)next; i < len; i++) {
        char c = frame_json[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return THALOVANT_ERR_JSON;
        }
    }

    /* A HiveMessage frame carries the bus payload under "payload"; a bare
     * payload (the binary frame decoder's output) is the payload itself. */
    thalovant_json_tok payload;
    thalovant_json_tok msg_type;
    int rc = thalovant_json_scan_key(frame_json, &root, "msg_type", &msg_type);
    if (rc == THALOVANT_OK) {
        if (!thalovant_json_str_eq(frame_json, &msg_type, "bus")) {
            return THALOVANT_OK;
        }
        if (!read_container(frame_json, &root, "payload", THALOVANT_JSON_OBJECT, &payload)) {
            return THALOVANT_OK;
        }
    } else if (rc == THALOVANT_ERR_MISSING) {
        payload = root;
    } else {
        return rc;
    }

    thalovant_json_tok type;
    rc = thalovant_json_scan_key(frame_json, &payload, "type", &type);
    if (rc == THALOVANT_ERR_MISSING || (rc == THALOVANT_OK && type.type != THALOVANT_JSON_STRING)) {
        return THALOVANT_OK;
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    thalovant_intent_kind kind;
    if (thalovant_json_str_eq(frame_json, &type, THALOVANT_EVENT_INTENT_LIST_RESPONSE)) {
        kind = THALOVANT_INTENT_LIST_RESPONSE;
    } else if (thalovant_json_str_eq(frame_json, &type, THALOVANT_EVENT_INTENT_DESCRIBE_RESPONSE)) {
        kind = THALOVANT_INTENT_DESCRIBE_RESPONSE;
    } else if (thalovant_json_str_eq(frame_json, &type, THALOVANT_EVENT_POLICY_DENIED)) {
        kind = THALOVANT_INTENT_POLICY_DENIED;
    } else {
        return THALOVANT_OK;
    }

    thalovant_json_tok data;
    bool has_data = read_container(frame_json, &payload, "data", THALOVANT_JSON_OBJECT, &data);
    thalovant_json_tok context;
    bool has_context =
        read_container(frame_json, &payload, "context", THALOVANT_JSON_OBJECT, &context);
    thalovant_json_tok session;
    bool has_session =
        has_context && read_container(frame_json, &context, "session", THALOVANT_JSON_OBJECT, &session);

    /* Reply request id: context ?? context.session ?? data (the ask order). */
    rc = THALOVANT_ERR_MISSING;
    if (has_context) {
        rc = request_id_from(frame_json, &context, out->request_id, sizeof(out->request_id));
        if (rc == THALOVANT_ERR_MISSING && has_session) {
            rc = request_id_from(frame_json, &session, out->request_id, sizeof(out->request_id));
        }
    }
    if (rc == THALOVANT_ERR_MISSING && has_data) {
        rc = request_id_from(frame_json, &data, out->request_id, sizeof(out->request_id));
    }
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) {
        return rc;
    }
    if (request_id != NULL && rc == THALOVANT_OK && strcmp(out->request_id, request_id) != 0) {
        /* Another request's reply. A reply carrying no id at all is kept:
         * the contract matches it by content when a hub does not echo. */
        out->request_id[0] = '\0';
        return THALOVANT_OK;
    }

    /* Language: data.lang ?? context.lang ?? context.session.lang. */
    rc = THALOVANT_ERR_MISSING;
    if (has_data) {
        rc = read_text(frame_json, &data, "lang", out->lang, sizeof(out->lang));
    }
    if (rc == THALOVANT_ERR_MISSING && has_context) {
        rc = read_text(frame_json, &context, "lang", out->lang, sizeof(out->lang));
    }
    if (rc == THALOVANT_ERR_MISSING && has_session) {
        rc = read_text(frame_json, &session, "lang", out->lang, sizeof(out->lang));
    }
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) {
        return rc;
    }

    if (kind == THALOVANT_INTENT_POLICY_DENIED) {
        out->ok = false;
        if (has_data) {
            if ((rc = read_optional_text(frame_json, &data, "denied_type", out->denied_type,
                                         sizeof(out->denied_type))) != THALOVANT_OK)
                return rc;
            if ((rc = read_optional_text(frame_json, &data, "code", out->code,
                                         sizeof(out->code))) != THALOVANT_OK)
                return rc;
            if ((rc = read_optional_text(frame_json, &data, "reason", out->reason,
                                         sizeof(out->reason))) != THALOVANT_OK)
                return rc;
            thalovant_json_tok inner;
            thalovant_json_tok allowed;
            if (read_container(frame_json, &data, "data", THALOVANT_JSON_OBJECT, &inner) &&
                read_container(frame_json, &inner, "allowed", THALOVANT_JSON_ARRAY, &allowed)) {
                /* Count the message types, which are the string entries: a
                 * list of nothing but numbers and nulls names no type, and
                 * one too malformed to walk cannot be read out either. The
                 * denial still stands on denied_type/code/reason. */
                const char *json = frame_json + allowed.start;
                size_t json_len = (size_t)(allowed.end - allowed.start);
                int types = walk_allowed(json, json_len, allowed.size, NULL, NULL);
                if (types > 0) {
                    out->allowed_json = json;
                    out->allowed_len = json_len;
                    out->allowed_count = types;
                }
            }
        }
    } else if (has_data) {
        thalovant_json_tok ok;
        if (thalovant_json_scan_key(frame_json, &data, "ok", &ok) == THALOVANT_OK) {
            out->ok = thalovant_json_as_bool(frame_json, &ok, true);
        }
        if ((rc = read_optional_text(frame_json, &data, "error", out->error,
                                     sizeof(out->error))) != THALOVANT_OK)
            return rc;
        thalovant_json_tok items;
        const char *key = kind == THALOVANT_INTENT_LIST_RESPONSE ? "intents" : "definitions";
        if (read_container(frame_json, &data, key, THALOVANT_JSON_ARRAY, &items)) {
            out->items_json = frame_json + items.start;
            out->items_len = (size_t)(items.end - items.start);
            out->count = items.size;
        }
    }
    out->kind = kind;
    return THALOVANT_OK;
}

/* ------------------------------------------------------------- walkers */

/*
 * Common entry check for the two reply walkers. `refusal_is_error` says
 * what an `ok: false` reply means for this query: a listing that failed has
 * told us nothing, and answering it with zero rows would show a person a
 * hub able to do nothing, so it is an error; a describe that failed has
 * answered -- the hub does not know that registration -- and simply has no
 * definitions.
 */
static int walkable(const thalovant_intent_event *event, thalovant_intent_kind kind, bool has_fn,
                    bool refusal_is_error, thalovant_json_tok *items)
{
    if (event == NULL || !has_fn) {
        return THALOVANT_ERR_INVALID;
    }
    if (event->kind == THALOVANT_INTENT_POLICY_DENIED) {
        return THALOVANT_ERR_POLICY_DENIED;
    }
    if (event->kind != kind) {
        return THALOVANT_ERR_INVALID;
    }
    if (!event->ok) {
        return refusal_is_error ? THALOVANT_ERR_HUB_REFUSED : 0;
    }
    if (event->items_json == NULL || event->items_len == 0) {
        return 0;
    }
    items->type = THALOVANT_JSON_ARRAY;
    items->start = 0;
    items->end = (int)event->items_len;
    items->size = event->count;
    items->parent = -1;
    return 1;
}

/* 1 when the row names an intent, 0 to skip it, negative on error. */
static int fill_registration(const char *js, const thalovant_json_tok *row,
                             thalovant_intent_registration *out)
{
    memset(out, 0, sizeof(*out));
    if (row->type != THALOVANT_JSON_OBJECT) {
        return 0;
    }
    int rc = read_text(js, row, "skill_id", out->skill_id, sizeof(out->skill_id));
    if (rc == THALOVANT_ERR_MISSING) {
        return 0;
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    rc = read_text(js, row, "intent_name", out->intent_name, sizeof(out->intent_name));
    if (rc == THALOVANT_ERR_MISSING) {
        return 0;
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    if ((rc = read_optional_text(js, row, "lang", out->lang, sizeof(out->lang))) != THALOVANT_OK) {
        return rc;
    }
    if ((rc = read_optional_text(js, row, "method", out->method, sizeof(out->method))) !=
        THALOVANT_OK) {
        return rc;
    }
    out->engine = engine_of(out->method);
    thalovant_json_tok value;
    out->enabled = true;
    if (thalovant_json_scan_key(js, row, "enabled", &value) == THALOVANT_OK) {
        out->enabled = thalovant_json_as_bool(js, &value, true);
    }
    if ((rc = read_optional_text(js, row, "session_id", out->session_id,
                                 sizeof(out->session_id))) != THALOVANT_OK) {
        return rc;
    }
    if (out->session_id[0] == '\0') {
        strcpy(out->session_id, "default");
    }
    if (read_container(js, row, "definition", THALOVANT_JSON_OBJECT, &value)) {
        out->definition_json = js + value.start;
        out->definition_len = (size_t)(value.end - value.start);
    }
    return 1;
}

int thalovant_intent_list_rows(const thalovant_intent_event *event,
                               thalovant_intent_registration_fn fn, void *user)
{
    thalovant_json_tok items;
    int rc = walkable(event, THALOVANT_INTENT_LIST_RESPONSE, fn != NULL, true, &items);
    if (rc <= 0) {
        return rc;
    }
    const char *js = event->items_json;
    size_t cursor = 0;
    thalovant_json_tok row;
    int delivered = 0;
    while ((rc = thalovant_json_scan_next(js, &items, &cursor, &row)) == 1) {
        thalovant_intent_registration registration;
        rc = fill_registration(js, &row, &registration);
        if (rc < 0) {
            return rc;
        }
        if (rc == 0) {
            continue;
        }
        delivered++;
        if (!fn(&registration, user)) {
            return delivered;
        }
    }
    return rc < 0 ? rc : delivered;
}

/* 1 when the item carries a definition naming an intent, 0 to skip it. */
static int fill_definition(const char *js, const thalovant_json_tok *item,
                           thalovant_intent_definition *out)
{
    memset(out, 0, sizeof(*out));
    thalovant_json_tok definition;
    if (item->type != THALOVANT_JSON_OBJECT ||
        !read_container(js, item, "definition", THALOVANT_JSON_OBJECT, &definition)) {
        return 0;
    }
    int rc = read_text(js, &definition, "skill_id", out->skill_id, sizeof(out->skill_id));
    if (rc == THALOVANT_ERR_MISSING) {
        return 0;
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    rc = read_text(js, &definition, "intent_name", out->intent_name, sizeof(out->intent_name));
    if (rc == THALOVANT_ERR_MISSING) {
        return 0;
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    if ((rc = read_optional_text(js, &definition, "lang", out->lang, sizeof(out->lang))) !=
        THALOVANT_OK) {
        return rc;
    }
    /* item.method ?? definition.method */
    rc = read_text(js, item, "method", out->method, sizeof(out->method));
    if (rc == THALOVANT_ERR_MISSING) {
        rc = read_optional_text(js, &definition, "method", out->method, sizeof(out->method));
    }
    if (rc != THALOVANT_OK) {
        return rc;
    }
    out->engine = engine_of(out->method);
    out->definition_json = js + definition.start;
    out->definition_len = (size_t)(definition.end - definition.start);
    thalovant_json_tok samples;
    if (read_container(js, &definition, "samples", THALOVANT_JSON_ARRAY, &samples)) {
        out->sample_count = samples.size;
    }
    return 1;
}

int thalovant_intent_definitions(const thalovant_intent_event *event,
                                 thalovant_intent_definition_fn fn, void *user)
{
    thalovant_json_tok items;
    int rc = walkable(event, THALOVANT_INTENT_DESCRIBE_RESPONSE, fn != NULL, false, &items);
    if (rc <= 0) {
        return rc;
    }
    const char *js = event->items_json;
    size_t cursor = 0;
    thalovant_json_tok item;
    int delivered = 0;
    while ((rc = thalovant_json_scan_next(js, &items, &cursor, &item)) == 1) {
        thalovant_intent_definition definition;
        rc = fill_definition(js, &item, &definition);
        if (rc < 0) {
            return rc;
        }
        if (rc == 0) {
            continue;
        }
        delivered++;
        if (!fn(&definition, user)) {
            return delivered;
        }
    }
    return rc < 0 ? rc : delivered;
}

int thalovant_intent_samples(const char *definition_json, size_t len,
                             thalovant_intent_sample_fn fn, void *user)
{
    if (definition_json == NULL || fn == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    thalovant_json_tok definition;
    int rc = thalovant_json_scan(definition_json, len, 0, &definition);
    if (rc < 0) {
        return rc;
    }
    if (definition.type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }
    thalovant_json_tok samples;
    if (!read_container(definition_json, &definition, "samples", THALOVANT_JSON_ARRAY, &samples)) {
        return 0;
    }
    size_t cursor = 0;
    thalovant_json_tok item;
    int delivered = 0;
    while ((rc = thalovant_json_scan_next(definition_json, &samples, &cursor, &item)) == 1) {
        if (item.type != THALOVANT_JSON_STRING) {
            continue;
        }
        thalovant_intent_sample sample;
        int n = thalovant_json_as_string(definition_json, &item, sample.text, sizeof(sample.text));
        if (n < 0) {
            return n;
        }
        if (n == 0) {
            continue;
        }
        sample.index = delivered;
        sample.has_slot = strchr(sample.text, '{') != NULL;
        delivered++;
        if (!fn(&sample, user)) {
            return delivered;
        }
    }
    return rc < 0 ? rc : delivered;
}

/* --------------------------------------------------------- languages */

static void trimmed(const char *text, const char **start, const char **end)
{
    if (text == NULL) {
        text = "";
    }
    const char *last = text + strlen(text);
    while (text < last && isspace((unsigned char)*text)) {
        text++;
    }
    while (last > text && isspace((unsigned char)last[-1])) {
        last--;
    }
    *start = text;
    *end = last;
}

static int fold(char c)
{
    return c == '_' ? '-' : tolower((unsigned char)c);
}

bool thalovant_intent_same_language(const char *a, const char *b)
{
    const char *a_start;
    const char *a_end;
    const char *b_start;
    const char *b_end;
    trimmed(a, &a_start, &a_end);
    trimmed(b, &b_start, &b_end);
    if (a_end - a_start != b_end - b_start) {
        return false;
    }
    for (; a_start < a_end; a_start++, b_start++) {
        if (fold(*a_start) != fold(*b_start)) {
            return false;
        }
    }
    return true;
}
