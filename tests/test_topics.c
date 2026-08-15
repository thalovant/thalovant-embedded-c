/*
 * Topic derivation: `topic_prefix` is the full base and each channel is a
 * plain "<prefix>/in|out|status" suffix. Endpoint expectations follow the
 * `mqttConnectionEndpoint` scheme-upgrade / trailing-slash rules.
 */
#include "harness.h"
#include "thalovant/topics.h"

static thalovant_identity base_identity(void)
{
    thalovant_identity identity;
    memset(&identity, 0, sizeof(identity));
    strcpy(identity.access_key, "tlv-key-123");
    strcpy(identity.password, "pw");
    strcpy(identity.site_id, "kitchen");
    strcpy(identity.default_master, "https://hub.example.com");
    identity.default_port = 5679;
    identity.mqtt.present = true;
    strcpy(identity.mqtt.endpoint, "mqtt://broker.local");
    strcpy(identity.mqtt.username, "hub-user");
    strcpy(identity.mqtt.password, "pw");
    identity.mqtt.qos = 1;
    return identity;
}

static void check_topics(const thalovant_identity *identity, const char *inbound,
                         const char *outbound, const char *status)
{
    thalovant_mqtt_topics topics;
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(identity, &topics), THALOVANT_OK);
    CHECK_STR_EQ(topics.inbound, inbound);
    CHECK_STR_EQ(topics.outbound, outbound);
    CHECK_STR_EQ(topics.status, status);
}

static void test_prefix_plain(void)
{
    thalovant_identity identity = base_identity();
    strcpy(identity.mqtt.topic_prefix, "hivemind/hub-1/tlv-key-123");
    check_topics(&identity, "hivemind/hub-1/tlv-key-123/in", "hivemind/hub-1/tlv-key-123/out",
                 "hivemind/hub-1/tlv-key-123/status");
}

static void test_prefix_trims_slashes(void)
{
    thalovant_identity identity = base_identity();
    strcpy(identity.mqtt.topic_prefix, "/hivemind/hub-1/tlv-key-123/");
    check_topics(&identity, "hivemind/hub-1/tlv-key-123/in", "hivemind/hub-1/tlv-key-123/out",
                 "hivemind/hub-1/tlv-key-123/status");
}

static void test_prefix_trims_whitespace(void)
{
    thalovant_identity identity = base_identity();
    strcpy(identity.mqtt.topic_prefix, "  hivemind/hub-1/tlv-key-123  ");
    check_topics(&identity, "hivemind/hub-1/tlv-key-123/in", "hivemind/hub-1/tlv-key-123/out",
                 "hivemind/hub-1/tlv-key-123/status");
}

static void test_prefix_whitespace_only(void)
{
    thalovant_identity identity = base_identity();
    thalovant_mqtt_topics topics;
    strcpy(identity.mqtt.topic_prefix, "   \t  ");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_MISSING);
}

static void test_prefix_rejects_wildcards(void)
{
    thalovant_identity identity = base_identity();
    thalovant_mqtt_topics topics;
    strcpy(identity.mqtt.topic_prefix, "hivemind/#/tlv-key-123");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_INVALID);
    strcpy(identity.mqtt.topic_prefix, "hivemind/hub-1/+");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_INVALID);
}

static void test_prefix_rejects_control_chars(void)
{
    thalovant_identity identity = base_identity();
    thalovant_mqtt_topics topics;
    /* An interior control byte (SOH) must be rejected, never embedded. */
    strcpy(identity.mqtt.topic_prefix, "hivemind/hub-1/tlv\001key");
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_INVALID);
}

static void test_prefix_max_length(void)
{
    thalovant_identity identity = base_identity();
    /* The longest prefix that fills THALOVANT_MQTT_TOPIC_PREFIX_MAX must still
     * derive cleanly (no truncation): at the default limits prefix + "/status"
     * fits THALOVANT_TOPIC_MAX. */
    char longest[THALOVANT_MQTT_TOPIC_PREFIX_MAX];
    memset(longest, 'a', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = '\0';
    strcpy(identity.mqtt.topic_prefix, longest);

    thalovant_mqtt_topics topics;
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_OK);

    char expected[THALOVANT_TOPIC_MAX];
    snprintf(expected, sizeof(expected), "%s/status", longest);
    CHECK_STR_EQ(topics.status, expected);
    CHECK_INT_EQ(strlen(topics.status), strlen(longest) + 7);
}

static void test_missing_configuration(void)
{
    thalovant_identity identity = base_identity();
    thalovant_mqtt_topics topics;
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_MISSING);
    identity.mqtt.present = false;
    CHECK_INT_EQ(thalovant_mqtt_topics_derive(&identity, &topics), THALOVANT_ERR_MISSING);
}

static void test_endpoint(void)
{
    thalovant_identity identity = base_identity();
    char endpoint[THALOVANT_MQTT_ENDPOINT_MAX];
    CHECK(thalovant_mqtt_endpoint(&identity.mqtt, endpoint, sizeof(endpoint)) > 0);
    CHECK_STR_EQ(endpoint, "mqtt://broker.local");
    /* tls upgrades the scheme and a trailing '/' is stripped. */
    strcpy(identity.mqtt.endpoint, "mqtt://broker.local:1883/");
    identity.mqtt.tls = true;
    CHECK(thalovant_mqtt_endpoint(&identity.mqtt, endpoint, sizeof(endpoint)) > 0);
    CHECK_STR_EQ(endpoint, "mqtts://broker.local:1883");
    /* Already-secure endpoints are untouched. */
    strcpy(identity.mqtt.endpoint, "mqtts://mqtt.example.com:8883");
    CHECK(thalovant_mqtt_endpoint(&identity.mqtt, endpoint, sizeof(endpoint)) > 0);
    CHECK_STR_EQ(endpoint, "mqtts://mqtt.example.com:8883");
}

static void test_endpoint_parse(void)
{
    thalovant_endpoint parsed;
    CHECK_INT_EQ(thalovant_endpoint_parse("mqtts://mqtt.example.com:8883", &parsed), THALOVANT_OK);
    CHECK_STR_EQ(parsed.scheme, "mqtts");
    CHECK_STR_EQ(parsed.host, "mqtt.example.com");
    CHECK_INT_EQ(parsed.port, 8883);
    CHECK(parsed.tls);
    CHECK_INT_EQ(thalovant_endpoint_parse("mqtt://broker.local", &parsed), THALOVANT_OK);
    CHECK_INT_EQ(parsed.port, 1883);
    CHECK(!parsed.tls);
    CHECK_INT_EQ(thalovant_endpoint_parse("wss://hub.example.com/ws?x=1", &parsed), THALOVANT_OK);
    CHECK_STR_EQ(parsed.host, "hub.example.com");
    CHECK_INT_EQ(parsed.port, 443);
    CHECK_STR_EQ(parsed.path, "/ws?x=1");
    CHECK(parsed.tls);
    CHECK_INT_EQ(thalovant_endpoint_parse("mqtt://[::1]:1884", &parsed), THALOVANT_OK);
    CHECK_STR_EQ(parsed.host, "::1");
    CHECK_INT_EQ(parsed.port, 1884);
    CHECK_INT_EQ(thalovant_endpoint_parse("no-scheme", &parsed), THALOVANT_ERR_INVALID);
}

static void test_client_id(void)
{
    char client_id[64];
    CHECK(thalovant_mqtt_client_id("tlv key!123", client_id, sizeof(client_id)) > 0);
    CHECK_STR_EQ(client_id, "thalovant-tlv-key-123");
    CHECK_INT_EQ(thalovant_mqtt_client_id("", client_id, sizeof(client_id)),
                 THALOVANT_ERR_MISSING);
    /* Truncated to 48 key characters. */
    char long_key[80];
    memset(long_key, 'a', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    int len = thalovant_mqtt_client_id(long_key, client_id, sizeof(client_id));
    CHECK_INT_EQ(len, 10 + 48);
}

void tlv_test_topics(void)
{
    test_prefix_plain();
    test_prefix_trims_slashes();
    test_prefix_trims_whitespace();
    test_prefix_whitespace_only();
    test_prefix_rejects_wildcards();
    test_prefix_rejects_control_chars();
    test_prefix_max_length();
    test_missing_configuration();
    test_endpoint();
    test_endpoint_parse();
    test_client_id();
}
