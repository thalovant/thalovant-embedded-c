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

void tlv_test_json(void)
{
    test_basic_parse();
    test_syntax_errors();
    test_unescape();
    test_aliases_skip_null();
    test_coercion();
    test_skip_and_span();
}
