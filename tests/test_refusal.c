/*
 * What an ask does when the hub refuses it, against the vectors every SDK
 * shares: contracts/conformance/refusal-vectors.json in the Python SDK,
 * vendored here and turned into refusal_vectors.h by
 * tools/generate-refusal-vectors.mjs, so a changed expectation upstream has
 * something here to notice it.
 *
 * The frames are the ones a hub sends, so the fields are read the way a client
 * reads them rather than transcribed.
 */
#include "harness.h"
#include "thalovant/ask.h"
#include "refusal_vectors.h"

static void test_refusals_read_as_their_vectors_say(void)
{
    for (size_t i = 0; i < sizeof(REFUSAL_VECTORS) / sizeof(REFUSAL_VECTORS[0]); ++i) {
        const refusal_vector *want = &REFUSAL_VECTORS[i];
        thalovant_refusal got;
        CHECK_INT_EQ(thalovant_ask_refusal(want->frame, strlen(want->frame), NULL, &got), 0);
        CHECK_STR_EQ(got.denied_type, want->denied_type);
        CHECK_STR_EQ(got.code, want->code);
        CHECK_STR_EQ(got.reason, want->reason);
        CHECK(got.has_quota == want->has_quota);
        CHECK_STR_EQ(got.quota_period, want->period);
        CHECK(got.quota_limit == want->limit);
        CHECK(got.quota_used == want->used);
        CHECK(got.quota_reset_after == want->reset_after);
        for (size_t entry = 0; entry < want->allowed_count; ++entry) {
            char allowed[THALOVANT_ASK_TEXT_MAX];
            int length = thalovant_ask_refusal_allowed(want->frame, strlen(want->frame), entry,
                                                       allowed, sizeof(allowed));
            CHECK(length > 0);
            CHECK_STR_EQ(allowed, want->allowed[entry]);
        }
        /* Nothing past the list the hub sent, whatever else was in it. */
        char extra[THALOVANT_ASK_TEXT_MAX];
        CHECK_INT_EQ(thalovant_ask_refusal_allowed(want->frame, strlen(want->frame),
                                                   want->allowed_count, extra, sizeof(extra)),
                     THALOVANT_ERR_MISSING);
    }
}

static void test_an_unanswered_question_is_not_a_refusal(void)
{
    for (size_t i = 0; i < sizeof(UNANSWERED_VECTORS) / sizeof(UNANSWERED_VECTORS[0]); ++i) {
        const unanswered_vector *want = &UNANSWERED_VECTORS[i];
        thalovant_ask_event event;
        CHECK_INT_EQ(thalovant_ask_classify(want->frame, strlen(want->frame), NULL, &event), 0);
        /* The hub understood and has no skill for it: its own kind, and a
         * failure, but never a refusal. */
        CHECK(event.kind == THALOVANT_ASK_INTENT_FAILURE);
        CHECK(event.is_failure);
        thalovant_refusal refusal;
        CHECK_INT_EQ(thalovant_ask_refusal(want->frame, strlen(want->frame), NULL, &refusal),
                     THALOVANT_ERR_MISSING);
    }
}

static void test_a_denial_is_taken_only_by_the_ask_it_can_belong_to(void)
{
    for (size_t i = 0; i < sizeof(REFUSAL_CORRELATION_VECTORS) / sizeof(REFUSAL_CORRELATION_VECTORS[0]); ++i) {
        const refusal_correlation_vector *want = &REFUSAL_CORRELATION_VECTORS[i];
        CHECK(thalovant_refusal_belongs_to_ask(want->request_id, "req-own", want->denied_type,
                                               want->asks, want->queries, want->sends)
              == want->taken);
    }
}

static void test_the_grace_window_is_the_one_the_vectors_name(void)
{
    CHECK_INT_EQ(THALOVANT_UNTRACKED_UTTERANCE_GRACE_SECONDS, REFUSAL_GRACE_SECONDS);
}

void tlv_test_refusal(void)
{
    test_refusals_read_as_their_vectors_say();
    test_an_unanswered_question_is_not_a_refusal();
    test_a_denial_is_taken_only_by_the_ask_it_can_belong_to();
    test_the_grace_window_is_the_one_the_vectors_name();
}
