/*
 * Thalovant identity: the JSON document returned by the Thalovant API
 * (`ClientIdentifyResource`) or written to a satellite's identity file.
 *
 * The parser accepts the same field aliases as the Node and Go SDKs:
 *
 *   access_key      accessKey | api_key | key
 *   crypto_key      cryptoKey
 *   site_id         siteId | site
 *   default_master  defaultMaster | hub_http_host | host | master
 *   default_port    defaultPort | hub_http_port | port
 *   default_path    defaultPath | hub_http_path | path | uri_path
 *   public_key      publicKey
 *   mqtt.endpoint   broker_url | brokerUrl
 *   mqtt.username   broker_username | brokerUsername
 *   mqtt.password   broker_password | brokerPassword
 *   mqtt.*          snake_case | camelCase for topic_prefix, hub_id,
 *                   c2s_topic, s2c_topic, status_topic, hash_topics, qos
 *
 * All parsing is allocation-free: values land in the fixed-size buffers
 * below (sizes tunable via thalovant/config.h).
 */
#ifndef THALOVANT_IDENTITY_H
#define THALOVANT_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>

#include "thalovant/config.h"
#include "thalovant/error.h"

typedef struct {
    /* False when the identity carries no usable MQTT credentials
     * (endpoint, username, and password are all required, mirroring
     * `MqttBrokerCredentials.from` in the Node SDK). */
    bool present;
    char endpoint[THALOVANT_MQTT_ENDPOINT_MAX];
    char username[THALOVANT_MQTT_USERNAME_MAX];
    char password[THALOVANT_MQTT_PASSWORD_MAX];
    char topic_prefix[THALOVANT_MQTT_TOPIC_PREFIX_MAX];
    char hub_id[THALOVANT_MQTT_HUB_ID_MAX];
    char c2s_topic[THALOVANT_TOPIC_MAX];
    char s2c_topic[THALOVANT_TOPIC_MAX];
    char status_topic[THALOVANT_TOPIC_MAX];
    bool hash_topics; /* default false */
    int qos;          /* 0 or 1, default 1 */
    bool tls;         /* default: endpoint starts with "mqtts://" */
} thalovant_mqtt_credentials;

typedef struct {
    char access_key[THALOVANT_ACCESS_KEY_MAX];
    char password[THALOVANT_PASSWORD_MAX];
    char crypto_key[THALOVANT_CRYPTO_KEY_MAX];   /* optional; "" when absent */
    char site_id[THALOVANT_SITE_ID_MAX];
    char default_master[THALOVANT_MASTER_MAX];   /* trailing '/' stripped */
    char default_path[THALOVANT_PATH_MAX];       /* normalized to "/x" or "" */
    char public_key[THALOVANT_PUBLIC_KEY_MAX];   /* optional; "" when absent */
    int default_port;                            /* default 5679 */
    thalovant_mqtt_credentials mqtt;
} thalovant_identity;

/*
 * Parse an identity JSON document. `json` need not be NUL-terminated.
 * Returns THALOVANT_OK, THALOVANT_ERR_JSON on malformed JSON,
 * THALOVANT_ERR_MISSING when access_key/password/site_id/default_master is
 * absent, THALOVANT_ERR_NOMEM when a value overflows its buffer, and
 * THALOVANT_ERR_INVALID for a non-positive/non-integer default_port.
 */
int thalovant_identity_parse(const char *json, size_t len, thalovant_identity *out);

#endif /* THALOVANT_IDENTITY_H */
