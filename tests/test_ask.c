/*
 * The ask frame fixture is byte-identical to the Node SDK's emitBus output
 * for client.ask("what time is it") with pinned session/request ids
 * (captured from dist/src via JSON.stringify).
 */
#include "harness.h"
#include "thalovant/ask.h"
#include "reply_claim_vectors.h"

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

    /* Legacy Mycroft name still classifies as an intent failure. */
    const char *intent_failure =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"complete_intent_failure\",\"data\":{},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(intent_failure, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_ASK_INTENT_FAILURE);
    CHECK(event.is_failure);

    /* Current OVOS name (ovos.intent.unmatched) maps to the same failure so
     * an unmatched utterance is surfaced instead of waiting out the timeout. */
    const char *unmatched =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.unmatched\",\"data\":{},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(unmatched, "req-1", &event), THALOVANT_OK);
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

static void test_reply_claims(void)
{
    for (size_t i = 0; i < sizeof(REPLY_CLAIM_VECTORS) / sizeof(REPLY_CLAIM_VECTORS[0]); ++i) {
        const reply_claim_vector *v = &REPLY_CLAIM_VECTORS[i];
        CHECK(thalovant_reply_claimed(v->handled, v->failed, v->stages, v->count) == v->claimed);
    }
    CHECK(!thalovant_reply_claimed(true, false, NULL, 1));
    CHECK(thalovant_reply_claimed(true, false, NULL, 0));
    char out[64];
    const char *frame = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"ovos-fallback-pipeline-plugin\",\"skill_id\":\"weather.skill\"}}}";
    CHECK_INT_EQ(thalovant_ask_event_pipeline_id(frame, strlen(frame), "r", out, sizeof(out)), 29);
    CHECK_STR_EQ(out, "ovos-fallback-pipeline-plugin");
    CHECK_INT_EQ(thalovant_ask_event_skill_id(frame, strlen(frame), "r", out, sizeof(out)), 13);
    CHECK_STR_EQ(out, "weather.skill");

    /* The shared cases, not one hand-written frame. `expected.skill_ids` used
       to be validated by nothing on this side: the header carried pipeline ids
       only, so editing it passed `make test` untouched. First-seen and unique,
       the same rule the other SDKs apply. */
    for (size_t i = 0; i < sizeof(REPLY_SKILL_VECTORS) / sizeof(REPLY_SKILL_VECTORS[0]); ++i) {
        const reply_skill_vector *v = &REPLY_SKILL_VECTORS[i];
        const char *seen[8];
        size_t count = 0;
        for (size_t f = 0; f < v->frames; ++f) {
            char id[128];
            int len = thalovant_ask_event_skill_id(v->frame[f], strlen(v->frame[f]), "r", id, sizeof(id));
            if (len <= 0) continue;
            bool already = false;
            for (size_t k = 0; k < count; ++k) {
                if (strcmp(seen[k], id) == 0) { already = true; break; }
            }
            if (already || count >= sizeof(seen) / sizeof(seen[0])) continue;
            /* Point at the vector's copy: `id` is reused every iteration. */
            CHECK(count < v->count);
            CHECK_STR_EQ(id, v->skills[count]);
            seen[count] = v->skills[count];
            count++;
        }
        CHECK_INT_EQ((int)count, (int)v->count);
    }
    CHECK_INT_EQ(thalovant_ask_event_pipeline_id(frame, strlen(frame), "other", out, sizeof(out)), THALOVANT_ERR_MISSING);
    CHECK_STR_EQ(out, "");
    CHECK_INT_EQ(thalovant_ask_event_pipeline_id(frame, strlen(frame), "r", out, 2), THALOVANT_ERR_NOMEM);
    CHECK_STR_EQ(out, "");
    CHECK_INT_EQ(thalovant_ask_event_pipeline_id(frame, strlen(frame), "r", NULL, 4), THALOVANT_ERR_INVALID);
    { const char *bad = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{},\"context\":{\"pipeline_id\":[]}}}";
      CHECK_INT_EQ(thalovant_ask_event_pipeline_id(bad, strlen(bad), NULL, out, sizeof(out)), THALOVANT_ERR_MISSING); CHECK_STR_EQ(out, ""); }
    { const char *bad = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{},\"context\":{\"pipeline_id\":\"\"}}}";
      CHECK_INT_EQ(thalovant_ask_event_pipeline_id(bad, strlen(bad), NULL, out, sizeof(out)), THALOVANT_ERR_MISSING); CHECK_STR_EQ(out, ""); }
    { const char *bad = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{},\"context\":{\"pipeline_id\":\"a\\u0000fallback\"}}}";
      CHECK_INT_EQ(thalovant_ask_event_pipeline_id(bad, strlen(bad), NULL, out, sizeof(out)), THALOVANT_ERR_INVALID); CHECK_STR_EQ(out, ""); }
}

void tlv_test_ask(void)
{
    test_reply_claims();
    test_build_frame_matches_node();
    test_build_payload_escaping_and_defaults();
    test_classify_speak();
    test_classify_correlation();
    test_classify_terminal_events();
    test_normalize_text();
}
