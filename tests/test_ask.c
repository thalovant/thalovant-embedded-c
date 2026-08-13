/*
 * The ask frame fixture is byte-identical to the Node SDK's emitBus output
 * for client.ask("what time is it") with pinned session/request ids
 * (captured from dist/src via JSON.stringify).
 */
#include "harness.h"
#include "thalovant/ask.h"

static const char ASK_FRAME_FIXTURE[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"recognizer_loop:utterance\",\"data\":"
    "{\"utterances\":[\"what time is it\"],\"lang\":\"en-us\"},\"context\":{\"request_id\":"
    "\"req-1\",\"thalovant_request_id\":\"req-1\",\"session\":{\"session_id\":\"sess-1\","
    "\"site_id\":\"kitchen\",\"lang\":\"en-us\",\"request_id\":\"req-1\"}}},\"metadata\":{},"
    "\"route\":[],\"node\":null,\"target_site_id\":null,\"target_pubkey\":null,"
    "\"source_peer\":null}";

static void test_build_frame_matches_node(void)
{
    thalovant_ask_request request = { "what time is it", "en-us", "sess-1", "kitchen", "req-1" };
    char out[1024];
    int len = thalovant_ask_build_frame(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, ASK_FRAME_FIXTURE);
}

static void test_build_payload_escaping_and_defaults(void)
{
    thalovant_ask_request request = { "say \"hi\"", NULL, "s", NULL, "r" };
    char out[1024];
    int len = thalovant_ask_build_payload(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"type\":\"recognizer_loop:utterance\",\"data\":{\"utterances\":"
                      "[\"say \\\"hi\\\"\"],\"lang\":\"en-us\"},\"context\":{\"request_id\":"
                      "\"r\",\"thalovant_request_id\":\"r\",\"session\":{\"session_id\":\"s\","
                      "\"lang\":\"en-us\",\"request_id\":\"r\"}}}");
    thalovant_ask_request invalid = { "", "en-us", "s", NULL, "r" };
    CHECK_INT_EQ(thalovant_ask_build_payload(&invalid, out, sizeof(out)),
                 THALOVANT_ERR_INVALID);
}

static int classify(const char *frame, const char *request_id, thalovant_ask_event *event)
{
    return thalovant_ask_classify(frame, strlen(frame), request_id, event);
}

static void test_classify_speak(void)
{
    const char *speak =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":"
        "\"It is noon.\"},\"context\":{\"request_id\":\"req-1\"}}}";
    thalovant_ask_event event;
    CHECK_INT_EQ(classify(speak, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_SPEAK);
    CHECK(!event.is_failure);
    CHECK_STR_EQ(event.text, "It is noon.");
    CHECK_STR_EQ(event.request_id, "req-1");

    /* ovos.utterance.speak counts as speak; request id may live in the
     * session or in data (Node's requestIdFromMapping fallbacks). */
    const char *speak_session =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.utterance.speak\",\"data\":"
        "{\"utterances\":[\"One\",\"Two\"]},\"context\":{\"session\":"
        "{\"thalovant_request_id\":\"req-1\"}}}}";
    CHECK_INT_EQ(classify(speak_session, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_SPEAK);
    CHECK_STR_EQ(event.text, "One");

    const char *speak_data =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"text\":\"hi\","
        "\"correlation_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(speak_data, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_SPEAK);
    CHECK_STR_EQ(event.text, "hi");
}

static void test_classify_correlation(void)
{
    thalovant_ask_event event;
    /* Different request id: ignored. */
    const char *other =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},"
        "\"context\":{\"request_id\":\"req-2\"}}}";
    CHECK_INT_EQ(classify(other, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_IGNORE);
    /* No request id at all: ignored under required correlation. */
    const char *anonymous =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"}}}";
    CHECK_INT_EQ(classify(anonymous, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_IGNORE);
    /* ...but accepted when the caller does not require correlation. */
    CHECK_INT_EQ(classify(anonymous, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_SPEAK);
    /* Non-bus frames and unknown bus events are ignored. */
    const char *query = "{\"msg_type\":\"query\",\"payload\":{\"type\":\"speak\"}}";
    CHECK_INT_EQ(classify(query, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_IGNORE);
    const char *unknown =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"mic.level\",\"data\":{}}}";
    CHECK_INT_EQ(classify(unknown, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_IGNORE);
}

static void test_classify_terminal_events(void)
{
    thalovant_ask_event event;
    const char *handled =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.utterance.handled\",\"data\":{},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(handled, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_HANDLED);
    CHECK(!event.is_failure);

    const char *intent_failure =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"complete_intent_failure\",\"data\":{},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(intent_failure, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_INTENT_FAILURE);
    CHECK(event.is_failure);

    const char *denied =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
        "{\"text\":\"policy says no\"},\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(denied, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_POLICY_DENIED);
    CHECK(event.is_failure);
    CHECK_STR_EQ(event.text, "policy says no");

    const char *timeout =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.query.timeout\",\"data\":{},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(timeout, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_QUERY_TIMEOUT);
    CHECK(event.is_failure);
}

static void test_normalize_text(void)
{
    char text[] = "  It   is\tnoon. \n";
    CHECK_INT_EQ(thalovant_ask_normalize_text(text), 11);
    CHECK_STR_EQ(text, "It is noon.");
    char empty[] = "   ";
    CHECK_INT_EQ(thalovant_ask_normalize_text(empty), 0);
    CHECK_STR_EQ(empty, "");
}

void tlv_test_ask(void)
{
    test_build_frame_matches_node();
    test_build_payload_escaping_and_defaults();
    test_classify_speak();
    test_classify_correlation();
    test_classify_terminal_events();
    test_normalize_text();
}
