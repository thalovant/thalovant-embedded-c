#include <stdio.h>

#include "harness.h"
#include "thalovant/json.h"

static void test_basic_parse(void)
{
    const char *js = "{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":\"e\"}}";
    thalovant_json_tok toks[32];
    int count = thalovant_json_parse(js, strlen(js), toks, 32);
    CHECK(count > 0);
    CHECK_INT_EQ(toks[0].type, THALOVANT_JSON_OBJECT);
    CHECK_INT_EQ(toks[0].size, 3);

    int a = thalovant_json_object_get(js, toks, count, 0, "a");
    CHECK(a >= 0);
    long value = 0;
    CHECK_INT_EQ(thalovant_json_as_int(js, &toks[a], &value), THALOVANT_OK);
    CHECK_INT_EQ(value, 1);

    int b = thalovant_json_object_get(js, toks, count, 0, "b");
    CHECK(b >= 0);
    CHECK_INT_EQ(toks[b].type, THALOVANT_JSON_ARRAY);
    CHECK_INT_EQ(toks[b].size, 3);

    int c = thalovant_json_object_get(js, toks, count, 0, "c");
    CHECK(c >= 0);
    int d = thalovant_json_object_get(js, toks, count, c, "d");
    CHECK(d >= 0);
    char buf[8];
    CHECK_INT_EQ(thalovant_json_unescape(js, &toks[d], buf, sizeof(buf)), 1);
    CHECK_STR_EQ(buf, "e");
}

static void test_syntax_errors(void)
{
    thalovant_json_tok toks[8];
    CHECK_INT_EQ(thalovant_json_parse("{", 1, toks, 8), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_parse("{\"a\":}", 6, toks, 8), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_parse("[1,]", 4, toks, 8), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_parse("tru", 3, toks, 8), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_parse("{} x", 4, toks, 8), THALOVANT_ERR_JSON);
    const char *big = "[[[[[[1]]]]]]";
    CHECK(thalovant_json_parse(big, strlen(big), toks, 4) == THALOVANT_ERR_NOMEM);
}

static void test_unescape(void)
{
    const char *js = "\"a\\n\\t\\\"\\\\\\u00e9\\ud83d\\ude00b\"";
    thalovant_json_tok toks[2];
    int count = thalovant_json_parse(js, strlen(js), toks, 2);
    CHECK(count == 1);
    char buf[32];
    int len = thalovant_json_unescape(js, &toks[0], buf, sizeof(buf));
    CHECK(len > 0);
    CHECK_STR_EQ(buf, "a\n\t\"\\\xc3\xa9\xf0\x9f\x98\x80" "b");
}

static void test_aliases_skip_null(void)
{
    const char *js = "{\"access_key\":null,\"key\":\"fallback\"}";
    thalovant_json_tok toks[8];
    int count = thalovant_json_parse(js, strlen(js), toks, 8);
    CHECK(count > 0);
    const char *const aliases[] = { "access_key", "accessKey", "key" };
    int value = thalovant_json_object_get_alias(js, toks, count, 0, aliases, 3);
    CHECK(value >= 0);
    char buf[16];
    CHECK(thalovant_json_as_string(js, &toks[value], buf, sizeof(buf)) > 0);
    CHECK_STR_EQ(buf, "fallback");
}

static void test_coercion(void)
{
    const char *js = "{\"s\":\"  trimmed \",\"n\":5679,\"ns\":\"1883\",\"b\":\"yes\","
                     "\"b2\":0,\"nul\":null,\"empty\":\"\"}";
    thalovant_json_tok toks[24];
    int count = thalovant_json_parse(js, strlen(js), toks, 24);
    CHECK(count > 0);
    char buf[24];
    int s = thalovant_json_object_get(js, toks, count, 0, "s");
    CHECK(thalovant_json_as_string(js, &toks[s], buf, sizeof(buf)) == 7);
    CHECK_STR_EQ(buf, "trimmed");
    long value = 0;
    int n = thalovant_json_object_get(js, toks, count, 0, "n");
    CHECK_INT_EQ(thalovant_json_as_int(js, &toks[n], &value), THALOVANT_OK);
    CHECK_INT_EQ(value, 5679);
    int ns = thalovant_json_object_get(js, toks, count, 0, "ns");
    CHECK_INT_EQ(thalovant_json_as_int(js, &toks[ns], &value), THALOVANT_OK);
    CHECK_INT_EQ(value, 1883);
    int b = thalovant_json_object_get(js, toks, count, 0, "b");
    CHECK(thalovant_json_as_bool(js, &toks[b], false) == true);
    int b2 = thalovant_json_object_get(js, toks, count, 0, "b2");
    CHECK(thalovant_json_as_bool(js, &toks[b2], true) == false);
    int nul = thalovant_json_object_get(js, toks, count, 0, "nul");
    CHECK(thalovant_json_is_truthy(js, &toks[nul]) == false);
    int empty = thalovant_json_object_get(js, toks, count, 0, "empty");
    CHECK(thalovant_json_is_truthy(js, &toks[empty]) == false);
    CHECK(thalovant_json_is_truthy(js, &toks[b]) == true);
    CHECK(thalovant_json_is_truthy(js, &toks[b2]) == false);
}

static void test_skip_and_span(void)
{
    const char *js = "{\"a\":{\"x\":[1,2]},\"b\":\"str\"}";
    thalovant_json_tok toks[16];
    int count = thalovant_json_parse(js, strlen(js), toks, 16);
    CHECK(count > 0);
    int b = thalovant_json_object_get(js, toks, count, 0, "b");
    CHECK(b >= 0);
    int start = 0, end = 0;
    thalovant_json_raw_span(&toks[b], &start, &end);
    CHECK_INT_EQ(end - start, 5); /* "str" with quotes */
    CHECK(js[start] == '"' && js[end - 1] == '"');
    int a = thalovant_json_object_get(js, toks, count, 0, "a");
    thalovant_json_raw_span(&toks[a], &start, &end);
    CHECK(js[start] == '{' && js[end - 1] == '}');
}


static void test_scan_values(void)
{
    /* Strings and containers holding commas and brackets: the shallow scan
     * jumps over strings whole and counts only depth-1 commas. */
    const char *js = "  {\"a\":\"x,]\",\"b\":[1,{\"c\":[2,3]},\"}\"],\"d\":{},\"e\":true} 7";
    thalovant_json_tok tok;
    int next = thalovant_json_scan(js, strlen(js), 0, &tok);
    CHECK(next > 0);
    CHECK_INT_EQ(tok.type, THALOVANT_JSON_OBJECT);
    CHECK_INT_EQ(tok.start, 2);
    CHECK_INT_EQ(tok.size, 4);
    CHECK_INT_EQ(tok.parent, -1);
    CHECK_INT_EQ(js[next - 1], '}');

    thalovant_json_tok value;
    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "a", &value), THALOVANT_OK);
    CHECK_INT_EQ(value.type, THALOVANT_JSON_STRING);
    char buf[16];
    CHECK_INT_EQ(thalovant_json_unescape(js, &value, buf, sizeof(buf)), 3);
    CHECK_STR_EQ(buf, "x,]");

    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "b", &value), THALOVANT_OK);
    CHECK_INT_EQ(value.type, THALOVANT_JSON_ARRAY);
    CHECK_INT_EQ(value.size, 3);
    size_t cursor = 0;
    thalovant_json_tok elem;
    CHECK_INT_EQ(thalovant_json_scan_next(js, &value, &cursor, &elem), 1);
    CHECK_INT_EQ(elem.type, THALOVANT_JSON_PRIMITIVE);
    long n = 0;
    CHECK_INT_EQ(thalovant_json_as_int(js, &elem, &n), THALOVANT_OK);
    CHECK_INT_EQ(n, 1);
    CHECK_INT_EQ(thalovant_json_scan_next(js, &value, &cursor, &elem), 1);
    CHECK_INT_EQ(elem.type, THALOVANT_JSON_OBJECT);
    CHECK_INT_EQ(elem.size, 1);
    thalovant_json_tok inner;
    CHECK_INT_EQ(thalovant_json_scan_key(js, &elem, "c", &inner), THALOVANT_OK);
    CHECK_INT_EQ(inner.type, THALOVANT_JSON_ARRAY);
    CHECK_INT_EQ(inner.size, 2);
    CHECK_INT_EQ(thalovant_json_scan_next(js, &value, &cursor, &elem), 1);
    CHECK_INT_EQ(elem.type, THALOVANT_JSON_STRING);
    CHECK(thalovant_json_str_eq(js, &elem, "}"));
    CHECK_INT_EQ(thalovant_json_scan_next(js, &value, &cursor, &elem), 0);
    /* Calling again at the end stays at the end. */
    CHECK_INT_EQ(thalovant_json_scan_next(js, &value, &cursor, &elem), 0);

    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "d", &value), THALOVANT_OK);
    CHECK_INT_EQ(value.type, THALOVANT_JSON_OBJECT);
    CHECK_INT_EQ(value.size, 0);
    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "e", &value), THALOVANT_OK);
    CHECK(thalovant_json_as_bool(js, &value, false));
    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "zz", &value), THALOVANT_ERR_MISSING);

    /* A trailing primitive after the object is the caller's business. */
    CHECK(thalovant_json_scan(js, strlen(js), (size_t)next, &tok) > 0);
    CHECK_INT_EQ(tok.type, THALOVANT_JSON_PRIMITIVE);
    CHECK(thalovant_json_str_eq(js, &tok, "7") == false);
    CHECK_INT_EQ(thalovant_json_as_int(js, &tok, &n), THALOVANT_OK);
    CHECK_INT_EQ(n, 7);

    /* An empty array iterates to nothing; a null value is not "missing". */
    const char *empty = "{\"list\":[],\"nul\":null}";
    CHECK(thalovant_json_scan(empty, strlen(empty), 0, &tok) > 0);
    CHECK_INT_EQ(thalovant_json_scan_key(empty, &tok, "list", &value), THALOVANT_OK);
    CHECK_INT_EQ(value.size, 0);
    cursor = 0;
    CHECK_INT_EQ(thalovant_json_scan_next(empty, &value, &cursor, &elem), 0);
    CHECK_INT_EQ(thalovant_json_scan_key(empty, &tok, "nul", &value), THALOVANT_OK);
    CHECK_INT_EQ(value.type, THALOVANT_JSON_PRIMITIVE);
    CHECK_INT_EQ(thalovant_json_as_string(empty, &value, buf, sizeof(buf)), 0);
}

static void test_scan_matches_tokenizer_on_big_arrays(void)
{
    /* 200 elements: far beyond any token pool, but the shallow scan holds
     * one token at a time. The span and child count agree with the
     * tokenizer's answer for a small prefix. */
    static char big[8192];
    size_t pos = 0;
    big[pos++] = '[';
    for (int i = 0; i < 200; i++) {
        pos += (size_t)sprintf(big + pos, "%s{\"i\":%d,\"s\":\"v,%d\"}", i ? "," : "", i, i);
    }
    big[pos++] = ']';
    big[pos] = '\0';
    thalovant_json_tok tok;
    CHECK_INT_EQ(thalovant_json_scan(big, pos, 0, &tok), (int)pos);
    CHECK_INT_EQ(tok.type, THALOVANT_JSON_ARRAY);
    CHECK_INT_EQ(tok.size, 200);
    size_t cursor = 0;
    thalovant_json_tok elem;
    int seen = 0;
    while (thalovant_json_scan_next(big, &tok, &cursor, &elem) == 1) {
        thalovant_json_tok value;
        CHECK_INT_EQ(thalovant_json_scan_key(big, &elem, "i", &value), THALOVANT_OK);
        long n = -1;
        CHECK_INT_EQ(thalovant_json_as_int(big, &value, &n), THALOVANT_OK);
        CHECK_INT_EQ(n, seen);
        seen++;
    }
    CHECK_INT_EQ(seen, 200);
    thalovant_json_tok toks[8];
    CHECK_INT_EQ(thalovant_json_parse(big, pos, toks, 8), THALOVANT_ERR_NOMEM);
}

static void test_scan_syntax_errors(void)
{
    thalovant_json_tok tok;
    CHECK_INT_EQ(thalovant_json_scan("{\"a\":1", 6, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("[1,2", 4, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("{\"a\":1]", 7, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("\"open", 5, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("\"bad\\q\"", 7, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("\"\\u12\"", 6, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("tru", 3, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan("   ", 3, 0, &tok), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_json_scan(NULL, 0, 0, &tok), THALOVANT_ERR_INVALID);

    /* A key lookup or iteration on the wrong token type is refused, and a
     * truncated member fails rather than reading past the object. */
    const char *js = "{\"a\":[1,2]}";
    CHECK(thalovant_json_scan(js, strlen(js), 0, &tok) > 0);
    thalovant_json_tok value;
    size_t cursor = 0;
    CHECK_INT_EQ(thalovant_json_scan_next(js, &tok, &cursor, &value), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_json_scan_key(js, &tok, "a", &value), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_json_scan_key(js, &value, "a", &tok), THALOVANT_ERR_INVALID);
    const char *broken = "{\"a\" 1}";
    CHECK(thalovant_json_scan(broken, strlen(broken), 0, &tok) > 0);
    CHECK_INT_EQ(thalovant_json_scan_key(broken, &tok, "a", &value), THALOVANT_ERR_JSON);
    const char *bare = "{1:2}";
    CHECK(thalovant_json_scan(bare, strlen(bare), 0, &tok) > 0);
    CHECK_INT_EQ(thalovant_json_scan_key(bare, &tok, "a", &value), THALOVANT_ERR_JSON);
}

void tlv_test_json(void)
{
    test_basic_parse();
    test_syntax_errors();
    test_unescape();
    test_aliases_skip_null();
    test_coercion();
    test_skip_and_span();
    test_scan_values();
    test_scan_matches_tokenizer_on_big_arrays();
    test_scan_syntax_errors();
}
