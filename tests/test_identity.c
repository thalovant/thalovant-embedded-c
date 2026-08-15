#include "harness.h"
#include "thalovant/identity.h"

/* Shape of ClientIdentifyResource from the Thalovant API (clients.py). */
static const char FIXTURE_API[] =
    "{"
    "\"password\":\"pw-secret\","
    "\"access_key\":\"tlv-key-123\","
    "\"crypto_key\":\"0123456789abcdef\","
    "\"site_id\":\"kitchen\","
    "\"default_port\":5679,"
    "\"default_master\":\"https://hub.example.com/\","
    "\"mqtt\":{"
    "\"endpoint\":\"mqtts://mqtt.example.com:8883\","
    "\"username\":\"hub-user\","
    "\"password\":\"mqtt-pw\","
    "\"topic_prefix\":\"hivemind/hub-1\","
    "\"tls\":true"
    "}"
    "}";

static void test_api_fixture(void)
{
    thalovant_identity identity;
    CHECK_INT_EQ(thalovant_identity_parse(FIXTURE_API, sizeof(FIXTURE_API) - 1, &identity),
                 THALOVANT_OK);
    CHECK_STR_EQ(identity.access_key, "tlv-key-123");
    CHECK_STR_EQ(identity.password, "pw-secret");
    CHECK_STR_EQ(identity.crypto_key, "0123456789abcdef");
    CHECK_STR_EQ(identity.site_id, "kitchen");
    CHECK_STR_EQ(identity.default_master, "https://hub.example.com");
    CHECK_INT_EQ(identity.default_port, 5679);
    CHECK_STR_EQ(identity.default_path, "");
    CHECK(identity.mqtt.present);
    CHECK_STR_EQ(identity.mqtt.endpoint, "mqtts://mqtt.example.com:8883");
    CHECK_STR_EQ(identity.mqtt.username, "hub-user");
    CHECK_STR_EQ(identity.mqtt.password, "mqtt-pw");
    CHECK_STR_EQ(identity.mqtt.topic_prefix, "hivemind/hub-1");
    CHECK(identity.mqtt.tls);
    CHECK_INT_EQ(identity.mqtt.qos, 1);
}

static void test_aliases(void)
{
    const char *js = "{"
                     "\"key\":\"k\",\"password\":\"p\",\"site\":\"s\","
                     "\"host\":\"hub.local\",\"port\":\"8080\",\"uri_path\":\"/api/\","
                     "\"cryptoKey\":\"c\",\"publicKey\":\"pub\","
                     "\"mqtt\":{\"broker_url\":\"mqtt://b\",\"brokerUsername\":\"u\","
                     "\"broker_password\":\"bp\",\"topicPrefix\":\"pre\",\"qos\":0}"
                     "}";
    thalovant_identity identity;
    CHECK_INT_EQ(thalovant_identity_parse(js, strlen(js), &identity), THALOVANT_OK);
    CHECK_STR_EQ(identity.access_key, "k");
    CHECK_STR_EQ(identity.site_id, "s");
    CHECK_STR_EQ(identity.default_master, "hub.local");
    CHECK_INT_EQ(identity.default_port, 8080);
    CHECK_STR_EQ(identity.default_path, "/api");
    CHECK_STR_EQ(identity.crypto_key, "c");
    CHECK_STR_EQ(identity.public_key, "pub");
    CHECK(identity.mqtt.present);
    CHECK_STR_EQ(identity.mqtt.endpoint, "mqtt://b");
    CHECK_STR_EQ(identity.mqtt.username, "u");
    CHECK_STR_EQ(identity.mqtt.password, "bp");
    CHECK_STR_EQ(identity.mqtt.topic_prefix, "pre");
    CHECK_INT_EQ(identity.mqtt.qos, 0);
    CHECK(!identity.mqtt.tls); /* mqtt:// scheme, no tls flag */
}

static void test_defaults_and_null_aliases(void)
{
    const char *js = "{\"access_key\":null,\"api_key\":\"a\",\"password\":\"p\","
                     "\"site_id\":\"s\",\"default_master\":\"m\"}";
    thalovant_identity identity;
    CHECK_INT_EQ(thalovant_identity_parse(js, strlen(js), &identity), THALOVANT_OK);
    CHECK_STR_EQ(identity.access_key, "a"); /* null falls through to api_key */
    CHECK_INT_EQ(identity.default_port, 5679);
    CHECK(!identity.mqtt.present);
    CHECK_STR_EQ(identity.crypto_key, "");
}

static void test_tls_defaults_from_scheme(void)
{
    const char *js = "{\"access_key\":\"a\",\"password\":\"p\",\"site_id\":\"s\","
                     "\"default_master\":\"m\","
                     "\"mqtt\":{\"endpoint\":\"mqtts://secure\",\"username\":\"u\","
                     "\"password\":\"pw\"}}";
    thalovant_identity identity;
    CHECK_INT_EQ(thalovant_identity_parse(js, strlen(js), &identity), THALOVANT_OK);
    CHECK(identity.mqtt.tls);
}

static void test_incomplete_mqtt_ignored(void)
{
    const char *js = "{\"access_key\":\"a\",\"password\":\"p\",\"site_id\":\"s\","
                     "\"default_master\":\"m\","
                     "\"mqtt\":{\"endpoint\":\"mqtt://b\",\"username\":\"u\"}}";
    thalovant_identity identity;
    CHECK_INT_EQ(thalovant_identity_parse(js, strlen(js), &identity), THALOVANT_OK);
    CHECK(!identity.mqtt.present); /* missing password: unusable */
}

static void test_errors(void)
{
    thalovant_identity identity;
    const char *missing = "{\"password\":\"p\",\"site_id\":\"s\",\"default_master\":\"m\"}";
    CHECK_INT_EQ(thalovant_identity_parse(missing, strlen(missing), &identity),
                 THALOVANT_ERR_MISSING);
    const char *bad_port = "{\"access_key\":\"a\",\"password\":\"p\",\"site_id\":\"s\","
                           "\"default_master\":\"m\",\"default_port\":\"junk\"}";
    CHECK_INT_EQ(thalovant_identity_parse(bad_port, strlen(bad_port), &identity),
                 THALOVANT_ERR_INVALID);
    const char *bad_json = "{\"access_key\":";
    CHECK_INT_EQ(thalovant_identity_parse(bad_json, strlen(bad_json), &identity),
                 THALOVANT_ERR_JSON);
    const char *not_object = "[1,2]";
    CHECK_INT_EQ(thalovant_identity_parse(not_object, strlen(not_object), &identity),
                 THALOVANT_ERR_JSON);
}

void tlv_test_identity(void)
{
    test_api_fixture();
    test_aliases();
    test_defaults_and_null_aliases();
    test_tls_defaults_from_scheme();
    test_incomplete_mqtt_ignored();
    test_errors();
}
