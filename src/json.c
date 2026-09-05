#include "thalovant/json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char *thalovant_err_str(int err)
{
    switch (err) {
    case THALOVANT_OK: return "ok";
    case THALOVANT_ERR_INVALID: return "invalid argument or input";
    case THALOVANT_ERR_NOMEM: return "buffer or token pool too small";
    case THALOVANT_ERR_JSON: return "JSON syntax error";
    case THALOVANT_ERR_MISSING: return "required field missing";
    case THALOVANT_ERR_AUTH: return "authentication tag mismatch";
    case THALOVANT_ERR_UNSUPPORTED: return "unsupported input";
    case THALOVANT_ERR_POLICY_DENIED: return "message type refused by the hub's policy";
    case THALOVANT_ERR_HUB_REFUSED: return "the hub refused the query";
    default: return "unknown error";
    }
}

typedef struct {
    const char *js;
    size_t len;
    size_t pos;
    thalovant_json_tok *toks;
    int max;
    int count;
} tlv_parser;

static void skip_ws(tlv_parser *p)
{
    while (p->pos < p->len) {
        char c = p->js[p->pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        p->pos++;
    }
}

static int alloc_tok(tlv_parser *p, thalovant_json_type type, int parent)
{
    if (p->count >= p->max) {
        return THALOVANT_ERR_NOMEM;
    }
    thalovant_json_tok *tok = &p->toks[p->count];
    tok->type = type;
    tok->start = (int)p->pos;
    tok->end = -1;
    tok->size = 0;
    tok->parent = parent;
    return p->count++;
}

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/*
 * Scan a string body: `pos` is the first byte after the opening quote.
 * Validates control characters and escapes exactly as the tokenizer
 * requires (so thalovant_json_unescape can trust the bytes) and stores the
 * offset of the closing quote in `close`.
 */
static int string_end(const char *js, size_t len, size_t pos, size_t *close)
{
    while (pos < len) {
        unsigned char c = (unsigned char)js[pos];
        if (c == '"') {
            *close = pos;
            return THALOVANT_OK;
        }
        if (c < 0x20) {
            return THALOVANT_ERR_JSON;
        }
        if (c == '\\') {
            pos++;
            if (pos >= len) {
                return THALOVANT_ERR_JSON;
            }
            char esc = js[pos];
            if (esc == 'u') {
                if (pos + 4 >= len) {
                    return THALOVANT_ERR_JSON;
                }
                for (int i = 1; i <= 4; i++) {
                    if (!is_hex_digit(js[pos + (size_t)i])) {
                        return THALOVANT_ERR_JSON;
                    }
                }
                pos += 4;
            } else if (strchr("\"\\/bfnrt", esc) == NULL) {
                return THALOVANT_ERR_JSON;
            }
        }
        pos++;
    }
    return THALOVANT_ERR_JSON;
}

/* p->pos must sit on the opening quote. */
static int parse_string(tlv_parser *p, int parent)
{
    p->pos++; /* opening quote */
    int idx = alloc_tok(p, THALOVANT_JSON_STRING, parent);
    if (idx < 0) {
        return idx;
    }
    size_t close;
    int rc = string_end(p->js, p->len, p->pos, &close);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    p->toks[idx].end = (int)close;
    p->pos = close + 1;
    return idx;
}

static bool is_primitive_end(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ']' || c == '}';
}

/* true/false/null or a JSON number; `n` bytes at `s`. */
static int check_primitive(const char *s, size_t n)
{
    if (n == 0) {
        return THALOVANT_ERR_JSON;
    }
    if (s[0] == 't' || s[0] == 'f' || s[0] == 'n') {
        if (!((n == 4 && strncmp(s, "true", 4) == 0) ||
              (n == 5 && strncmp(s, "false", 5) == 0) ||
              (n == 4 && strncmp(s, "null", 4) == 0))) {
            return THALOVANT_ERR_JSON;
        }
        return THALOVANT_OK;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
            return THALOVANT_ERR_JSON;
        }
    }
    if (!(s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
        return THALOVANT_ERR_JSON;
    }
    return THALOVANT_OK;
}

static int parse_primitive(tlv_parser *p, int parent)
{
    int idx = alloc_tok(p, THALOVANT_JSON_PRIMITIVE, parent);
    if (idx < 0) {
        return idx;
    }
    size_t start = p->pos;
    while (p->pos < p->len && !is_primitive_end(p->js[p->pos])) {
        p->pos++;
    }
    int rc = check_primitive(p->js + start, p->pos - start);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    p->toks[idx].end = (int)p->pos;
    return idx;
}

static int parse_value(tlv_parser *p, int parent, int depth);

static int parse_container(tlv_parser *p, int parent, int depth, bool object)
{
    int idx = alloc_tok(p, object ? THALOVANT_JSON_OBJECT : THALOVANT_JSON_ARRAY, parent);
    if (idx < 0) {
        return idx;
    }
    char close = object ? '}' : ']';
    p->pos++; /* opening brace/bracket */
    skip_ws(p);
    if (p->pos < p->len && p->js[p->pos] == close) {
        p->pos++;
        p->toks[idx].end = (int)p->pos;
        return idx;
    }
    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) {
            return THALOVANT_ERR_JSON;
        }
        if (object) {
            if (p->js[p->pos] != '"') {
                return THALOVANT_ERR_JSON;
            }
            int key = parse_string(p, idx);
            if (key < 0) {
                return key;
            }
            skip_ws(p);
            if (p->pos >= p->len || p->js[p->pos] != ':') {
                return THALOVANT_ERR_JSON;
            }
            p->pos++;
            int value = parse_value(p, key, depth + 1);
            if (value < 0) {
                return value;
            }
            p->toks[key].size = 1;
        } else {
            int value = parse_value(p, idx, depth + 1);
            if (value < 0) {
                return value;
            }
        }
        p->toks[idx].size++;
        skip_ws(p);
        if (p->pos >= p->len) {
            return THALOVANT_ERR_JSON;
        }
        if (p->js[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->js[p->pos] == close) {
            p->pos++;
            p->toks[idx].end = (int)p->pos;
            return idx;
        }
        return THALOVANT_ERR_JSON;
    }
}

static int parse_value(tlv_parser *p, int parent, int depth)
{
    if (depth > THALOVANT_JSON_MAX_DEPTH) {
        return THALOVANT_ERR_JSON;
    }
    skip_ws(p);
    if (p->pos >= p->len) {
        return THALOVANT_ERR_JSON;
    }
    char c = p->js[p->pos];
    if (c == '{') {
        return parse_container(p, parent, depth, true);
    }
    if (c == '[') {
        return parse_container(p, parent, depth, false);
    }
    if (c == '"') {
        return parse_string(p, parent);
    }
    return parse_primitive(p, parent);
}

int thalovant_json_parse(const char *js, size_t len, thalovant_json_tok *toks, int max_toks)
{
    if (js == NULL || toks == NULL || max_toks <= 0) {
        return THALOVANT_ERR_INVALID;
    }
    tlv_parser p = { js, len, 0, toks, max_toks, 0 };
    int root = parse_value(&p, -1, 0);
    if (root < 0) {
        return root;
    }
    skip_ws(&p);
    if (p.pos != p.len) {
        return THALOVANT_ERR_JSON;
    }
    return p.count;
}

int thalovant_json_skip(const thalovant_json_tok *toks, int count, int idx)
{
    if (idx < 0 || idx >= count) {
        return count;
    }
    int end = idx + 1;
    for (int i = 0; i < toks[idx].size && end < count; i++) {
        end = thalovant_json_skip(toks, count, end);
    }
    return end;
}

bool thalovant_json_str_eq(const char *js, const thalovant_json_tok *tok, const char *str)
{
    if (tok->type != THALOVANT_JSON_STRING) {
        return false;
    }
    size_t n = (size_t)(tok->end - tok->start);
    return strlen(str) == n && memcmp(js + tok->start, str, n) == 0;
}

int thalovant_json_object_get(const char *js, const thalovant_json_tok *toks, int count, int obj,
                              const char *key)
{
    if (obj < 0 || obj >= count || toks[obj].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_MISSING;
    }
    int child = obj + 1;
    for (int i = 0; i < toks[obj].size && child < count; i++) {
        if (thalovant_json_str_eq(js, &toks[child], key) && child + 1 < count) {
            return child + 1;
        }
        child = thalovant_json_skip(toks, count, child);
    }
    return THALOVANT_ERR_MISSING;
}

static bool tok_is_null(const char *js, const thalovant_json_tok *tok)
{
    return tok->type == THALOVANT_JSON_PRIMITIVE && tok->end - tok->start == 4 &&
           memcmp(js + tok->start, "null", 4) == 0;
}

int thalovant_json_object_get_alias(const char *js, const thalovant_json_tok *toks, int count,
                                    int obj, const char *const *aliases, size_t alias_count)
{
    for (size_t i = 0; i < alias_count; i++) {
        int value = thalovant_json_object_get(js, toks, count, obj, aliases[i]);
        if (value >= 0 && !tok_is_null(js, &toks[value])) {
            return value;
        }
    }
    return THALOVANT_ERR_MISSING;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int put_utf8(unsigned long cp, char *out, size_t cap, size_t *pos)
{
    unsigned char buf[4];
    size_t n;
    if (cp < 0x80) {
        buf[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xc0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (unsigned char)(0xe0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        buf[0] = (unsigned char)(0xf0 | (cp >> 18));
        buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        buf[3] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 4;
    }
    if (*pos + n >= cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out + *pos, buf, n);
    *pos += n;
    return THALOVANT_OK;
}

static unsigned long read_u16_escape(const char *s)
{
    unsigned long value = 0;
    for (int i = 0; i < 4; i++) {
        value = (value << 4) | (unsigned long)hex_val(s[i]);
    }
    return value;
}

int thalovant_json_unescape(const char *js, const thalovant_json_tok *tok, char *out, size_t cap)
{
    if (tok->type != THALOVANT_JSON_STRING || out == NULL || cap == 0) {
        return THALOVANT_ERR_INVALID;
    }
    size_t pos = 0;
    int i = tok->start;
    while (i < tok->end) {
        char c = js[i];
        if (c != '\\') {
            if (pos + 1 >= cap) {
                return THALOVANT_ERR_NOMEM;
            }
            out[pos++] = c;
            i++;
            continue;
        }
        i++;
        char esc = js[i];
        i++;
        char plain = 0;
        switch (esc) {
        case '"': plain = '"'; break;
        case '\\': plain = '\\'; break;
        case '/': plain = '/'; break;
        case 'b': plain = '\b'; break;
        case 'f': plain = '\f'; break;
        case 'n': plain = '\n'; break;
        case 'r': plain = '\r'; break;
        case 't': plain = '\t'; break;
        case 'u': {
            unsigned long cp = read_u16_escape(js + i);
            i += 4;
            if (cp >= 0xd800 && cp <= 0xdbff && i + 6 <= tok->end && js[i] == '\\' &&
                js[i + 1] == 'u') {
                unsigned long low = read_u16_escape(js + i + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    i += 6;
                }
            }
            int rc = put_utf8(cp, out, cap, &pos);
            if (rc != THALOVANT_OK) {
                return rc;
            }
            continue;
        }
        default:
            return THALOVANT_ERR_JSON;
        }
        if (pos + 1 >= cap) {
            return THALOVANT_ERR_NOMEM;
        }
        out[pos++] = plain;
    }
    out[pos] = '\0';
    return (int)pos;
}

static int trim_in_place(char *out, int len)
{
    int start = 0;
    while (start < len && isspace((unsigned char)out[start])) {
        start++;
    }
    int end = len;
    while (end > start && isspace((unsigned char)out[end - 1])) {
        end--;
    }
    int n = end - start;
    if (start > 0 && n > 0) {
        memmove(out, out + start, (size_t)n);
    }
    out[n] = '\0';
    return n;
}

int thalovant_json_as_string(const char *js, const thalovant_json_tok *tok, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return THALOVANT_ERR_INVALID;
    }
    if (tok->type == THALOVANT_JSON_STRING) {
        int len = thalovant_json_unescape(js, tok, out, cap);
        if (len < 0) {
            return len;
        }
        return trim_in_place(out, len);
    }
    if (tok->type == THALOVANT_JSON_PRIMITIVE) {
        if (tok_is_null(js, tok)) {
            out[0] = '\0';
            return 0;
        }
        size_t n = (size_t)(tok->end - tok->start);
        if (n + 1 > cap) {
            return THALOVANT_ERR_NOMEM;
        }
        memcpy(out, js + tok->start, n);
        out[n] = '\0';
        return trim_in_place(out, (int)n);
    }
    return THALOVANT_ERR_INVALID;
}

int thalovant_json_as_int(const char *js, const thalovant_json_tok *tok, long *out)
{
    char buf[32];
    int len = thalovant_json_as_string(js, tok, buf, sizeof(buf));
    if (len < 0) {
        return THALOVANT_ERR_INVALID;
    }
    if (len == 0) {
        return THALOVANT_ERR_MISSING;
    }
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || (end != NULL && *end != '\0')) {
        return THALOVANT_ERR_INVALID;
    }
    *out = value;
    return THALOVANT_OK;
}

bool thalovant_json_as_bool(const char *js, const thalovant_json_tok *tok, bool fallback)
{
    char buf[16];
    int len = thalovant_json_as_string(js, tok, buf, sizeof(buf));
    if (len < 0) {
        return fallback;
    }
    for (int i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    if (strcmp(buf, "1") == 0 || strcmp(buf, "true") == 0 || strcmp(buf, "yes") == 0 ||
        strcmp(buf, "on") == 0) {
        return true;
    }
    if (strcmp(buf, "0") == 0 || strcmp(buf, "false") == 0 || strcmp(buf, "no") == 0 ||
        strcmp(buf, "off") == 0) {
        return false;
    }
    /* Numeric coercion: any non-zero number is true. */
    char *end = NULL;
    double value = strtod(buf, &end);
    if (tok->type == THALOVANT_JSON_PRIMITIVE && end != buf && *end == '\0') {
        return value != 0.0;
    }
    return fallback;
}

bool thalovant_json_is_truthy(const char *js, const thalovant_json_tok *tok)
{
    if (tok->type == THALOVANT_JSON_OBJECT || tok->type == THALOVANT_JSON_ARRAY) {
        return true;
    }
    if (tok->type == THALOVANT_JSON_STRING) {
        return tok->end > tok->start;
    }
    if (tok->type == THALOVANT_JSON_PRIMITIVE) {
        size_t n = (size_t)(tok->end - tok->start);
        const char *s = js + tok->start;
        if ((n == 4 && memcmp(s, "null", 4) == 0) || (n == 5 && memcmp(s, "false", 5) == 0)) {
            return false;
        }
        if (n == 4 && memcmp(s, "true", 4) == 0) {
            return true;
        }
        char buf[32];
        if (thalovant_json_as_string(js, tok, buf, sizeof(buf)) < 0) {
            return true;
        }
        char *end = NULL;
        double value = strtod(buf, &end);
        if (end != buf && *end == '\0') {
            return value != 0.0;
        }
        return true;
    }
    return false;
}

void thalovant_json_raw_span(const thalovant_json_tok *tok, int *start, int *end)
{
    if (tok->type == THALOVANT_JSON_STRING) {
        *start = tok->start - 1;
        *end = tok->end + 1;
    } else {
        *start = tok->start;
        *end = tok->end;
    }
}

/* ------------------------------------------------------- shallow scans */

static size_t skip_ws_at(const char *js, size_t len, size_t pos)
{
    while (pos < len) {
        char c = js[pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        pos++;
    }
    return pos;
}

int thalovant_json_scan(const char *js, size_t len, size_t pos, thalovant_json_tok *tok)
{
    if (js == NULL || tok == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    pos = skip_ws_at(js, len, pos);
    if (pos >= len) {
        return THALOVANT_ERR_JSON;
    }
    tok->size = 0;
    tok->parent = -1;
    char c = js[pos];
    if (c == '"') {
        size_t close;
        int rc = string_end(js, len, pos + 1, &close);
        if (rc != THALOVANT_OK) {
            return rc;
        }
        tok->type = THALOVANT_JSON_STRING;
        tok->start = (int)pos + 1;
        tok->end = (int)close;
        return (int)close + 1;
    }
    if (c == '{' || c == '[') {
        /* Walk to the matching close bracket, jumping over strings whole;
         * commas seen at depth 1 count the direct children. */
        char close = c == '{' ? '}' : ']';
        int depth = 0;
        int commas = 0;
        bool empty = true;
        size_t i = pos;
        while (i < len) {
            char ch = js[i];
            if (ch == '"') {
                size_t end;
                int rc = string_end(js, len, i + 1, &end);
                if (rc != THALOVANT_OK) {
                    return rc;
                }
                empty = false;
                i = end + 1;
                continue;
            }
            if (ch == '{' || ch == '[') {
                if (depth > 0) {
                    empty = false;
                }
                depth++;
            } else if (ch == '}' || ch == ']') {
                depth--;
                if (depth == 0) {
                    if (ch != close) {
                        return THALOVANT_ERR_JSON;
                    }
                    i++;
                    break;
                }
            } else if (depth == 1) {
                if (ch == ',') {
                    commas++;
                } else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                    empty = false;
                }
            }
            i++;
        }
        if (depth != 0) {
            return THALOVANT_ERR_JSON;
        }
        tok->type = c == '{' ? THALOVANT_JSON_OBJECT : THALOVANT_JSON_ARRAY;
        tok->start = (int)pos;
        tok->end = (int)i;
        tok->size = empty ? 0 : commas + 1;
        return (int)i;
    }
    size_t end = pos;
    while (end < len && !is_primitive_end(js[end])) {
        end++;
    }
    int rc = check_primitive(js + pos, end - pos);
    if (rc != THALOVANT_OK) {
        return rc;
    }
    tok->type = THALOVANT_JSON_PRIMITIVE;
    tok->start = (int)pos;
    tok->end = (int)end;
    return (int)end;
}

int thalovant_json_scan_key(const char *js, const thalovant_json_tok *obj, const char *key,
                            thalovant_json_tok *value)
{
    if (js == NULL || obj == NULL || key == NULL || value == NULL ||
        obj->type != THALOVANT_JSON_OBJECT || obj->end <= obj->start) {
        return THALOVANT_ERR_INVALID;
    }
    size_t end = (size_t)obj->end;
    size_t pos = (size_t)obj->start + 1;
    bool first = true;
    for (;;) {
        pos = skip_ws_at(js, end, pos);
        if (pos >= end) {
            return THALOVANT_ERR_JSON;
        }
        if (js[pos] == '}') {
            return THALOVANT_ERR_MISSING;
        }
        if (!first) {
            /* Members are comma-separated: after one, the object either
             * closes (handled above) or exactly one comma introduces the
             * next. Anything else is malformed. */
            if (js[pos] != ',') {
                return THALOVANT_ERR_JSON;
            }
            pos = skip_ws_at(js, end, pos + 1);
            if (pos >= end) {
                return THALOVANT_ERR_JSON;
            }
        }
        first = false;
        /* A member starts with its key, so this also rejects a leading
         * comma, a doubled comma, and a trailing comma before '}'. */
        if (js[pos] != '"') {
            return THALOVANT_ERR_JSON;
        }
        thalovant_json_tok name;
        int next = thalovant_json_scan(js, end, pos, &name);
        if (next < 0) {
            return next;
        }
        pos = skip_ws_at(js, end, (size_t)next);
        if (pos >= end || js[pos] != ':') {
            return THALOVANT_ERR_JSON;
        }
        next = thalovant_json_scan(js, end, pos + 1, value);
        if (next < 0) {
            return next;
        }
        if (thalovant_json_str_eq(js, &name, key)) {
            return THALOVANT_OK;
        }
        pos = (size_t)next;
    }
}

int thalovant_json_scan_next(const char *js, const thalovant_json_tok *arr, size_t *cursor,
                             thalovant_json_tok *elem)
{
    if (js == NULL || arr == NULL || cursor == NULL || elem == NULL ||
        arr->type != THALOVANT_JSON_ARRAY || arr->end <= arr->start) {
        return THALOVANT_ERR_INVALID;
    }
    size_t end = (size_t)arr->end;
    bool first = *cursor == 0;
    size_t pos = skip_ws_at(js, end, first ? (size_t)arr->start + 1 : *cursor);
    if (pos >= end) {
        return THALOVANT_ERR_JSON;
    }
    if (js[pos] == ']') {
        /* The end of the array; calling again from here stays here. */
        *cursor = pos;
        return 0;
    }
    if (!first) {
        /* Elements are comma-separated: after one, the array either closes
         * (handled above) or exactly one comma introduces the next. */
        if (js[pos] != ',') {
            return THALOVANT_ERR_JSON;
        }
        pos = skip_ws_at(js, end, pos + 1);
        /* A trailing comma before ']' and a doubled comma are malformed. */
        if (pos >= end || js[pos] == ']' || js[pos] == ',') {
            return THALOVANT_ERR_JSON;
        }
    }
    int next = thalovant_json_scan(js, end, pos, elem);
    if (next < 0) {
        return next;
    }
    *cursor = (size_t)next;
    return 1;
}
