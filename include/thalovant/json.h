/*
 * Minimal JSON tokenizer written for this repository (no third-party code).
 *
 * The tokenizer fills a caller-provided token array with offsets into the
 * source text; it never allocates and never copies. String tokens cover the
 * *content* between the quotes (escapes still encoded); objects, arrays, and
 * primitives cover their full source span.
 *
 * Object tokens have `size` == number of key/value pairs; each key is a
 * string token with `size` == 1 whose single child is the value. Array
 * tokens have `size` == number of elements.
 */
#ifndef THALOVANT_JSON_H
#define THALOVANT_JSON_H

#include <stdbool.h>
#include <stddef.h>

#include "thalovant/config.h"
#include "thalovant/error.h"

typedef enum {
    THALOVANT_JSON_UNDEFINED = 0,
    THALOVANT_JSON_OBJECT,
    THALOVANT_JSON_ARRAY,
    THALOVANT_JSON_STRING,
    THALOVANT_JSON_PRIMITIVE,
} thalovant_json_type;

typedef struct {
    thalovant_json_type type;
    int start;  /* byte offset of the first byte */
    int end;    /* byte offset one past the last byte */
    int size;   /* children (see header comment) */
    int parent; /* token index of the parent, -1 for the root */
} thalovant_json_tok;

/*
 * Tokenize `js` (length `len`, need not be NUL-terminated). Returns the
 * number of tokens produced, THALOVANT_ERR_JSON on syntax errors, or
 * THALOVANT_ERR_NOMEM when `max_toks` is too small.
 */
int thalovant_json_parse(const char *js, size_t len, thalovant_json_tok *toks, int max_toks);

/* Index one past the subtree rooted at `idx` (i.e. the next sibling). */
int thalovant_json_skip(const thalovant_json_tok *toks, int count, int idx);

/* Raw byte-compare of a string token against a C string (no unescaping). */
bool thalovant_json_str_eq(const char *js, const thalovant_json_tok *tok, const char *str);

/*
 * Value token index for `key` in object token `obj`, or THALOVANT_ERR_MISSING.
 */
int thalovant_json_object_get(const char *js, const thalovant_json_tok *toks, int count, int obj,
                              const char *key);

/*
 * First alias (in alias order, mirroring `a ?? b ?? c` in the reference
 * SDKs) whose value exists and is not JSON null. THALOVANT_ERR_MISSING when
 * none match.
 */
int thalovant_json_object_get_alias(const char *js, const thalovant_json_tok *toks, int count,
                                    int obj, const char *const *aliases, size_t alias_count);

/*
 * Unescape a string token into `out` (NUL-terminated). Returns the decoded
 * length, THALOVANT_ERR_NOMEM if `cap` is too small, or THALOVANT_ERR_JSON
 * for invalid escapes.
 */
int thalovant_json_unescape(const char *js, const thalovant_json_tok *tok, char *out, size_t cap);

/*
 * String-coerce a value the way the reference SDKs do: strings are
 * unescaped, numbers/booleans copied verbatim, null becomes "". The result
 * is whitespace-trimmed. Objects/arrays yield THALOVANT_ERR_INVALID.
 */
int thalovant_json_as_string(const char *js, const thalovant_json_tok *tok, char *out, size_t cap);

/* Integer coercion accepting numbers and numeric strings. */
int thalovant_json_as_int(const char *js, const thalovant_json_tok *tok, long *out);

/*
 * Boolean coercion: true/false, non-zero numbers, and the strings
 * "1"/"true"/"yes"/"on" / "0"/"false"/"no"/"off"; anything else yields
 * `fallback`.
 */
bool thalovant_json_as_bool(const char *js, const thalovant_json_tok *tok, bool fallback);

/* JavaScript-style truthiness (null/false/0/"" are falsy). */
bool thalovant_json_is_truthy(const char *js, const thalovant_json_tok *tok);

/*
 * Full source span of a token including quotes for strings — useful for
 * slicing raw JSON out of a frame.
 */
void thalovant_json_raw_span(const thalovant_json_tok *tok, int *start, int *end);

#endif /* THALOVANT_JSON_H */
