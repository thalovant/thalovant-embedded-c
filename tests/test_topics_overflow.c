/*
 * Overflow coverage for thalovant_mqtt_topics_derive.
 *
 * The snprintf guard that returns THALOVANT_ERR_NOMEM when "<prefix>/<suffix>"
 * would exceed a derived-topic buffer is unreachable at the default limits:
 * THALOVANT_MQTT_TOPIC_PREFIX_MAX (160) + "/status" always fits in
 * THALOVANT_TOPIC_MAX (256). This translation unit rebuilds the topic code
 * with a deliberately tiny THALOVANT_TOPIC_MAX so the guard is exercised,
 * proving a too-long derived topic is rejected rather than silently truncated.
 *
 * It #includes src/topics.c directly (instead of linking libthalovant.a) so
 * the derived-topic buffers and the derive function share this small limit; it
 * is therefore built and run as its own binary, separate from the main suite.
 */
#define THALOVANT_TOPIC_MAX 24
#include "../src/topics.c"

#include "harness.h"

int tlv_test_checks = 0;
int tlv_test_failures = 0;

int main(void)
{
    thalovant_identity identity;
    memset(&identity, 0, sizeof(identity));
    identity.mqtt.present = true;

    thalovant_mqtt_topics topics;

    /*
     * prefix (19) + "/status" (7) + NUL = 27 > 24: the status channel cannot
     * fit, so derivation must fail with NOMEM rather than truncate the topic.
     */
    strcpy(identity.mqtt.topic_prefix, "hivemind/hub-1/abcd");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_NOMEM);

    /*
     * A prefix short enough that every channel fits still succeeds, so the
     * guard is not a false positive right at the boundary.
     */
    strcpy(identity.mqtt.topic_prefix, "hm/h/k");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_OK);
    CHECK_STR_EQ(topics.status, "hm/h/k/status");

    printf("%d checks, %d failures\n", tlv_test_checks, tlv_test_failures);
    return tlv_test_failures == 0 ? 0 : 1;
}
