/*
 * MQTT topic derivation — a faithful port of `mqttTopicsForIdentity` and
 * `mqttConnectionEndpoint` from the Node SDK (src/transport-mqtt.ts).
 *
 * Rules (in priority order):
 *  1. The satellite id is the access key, or — when `hash_topics` is set —
 *     the first 16 hex characters of sha256(access_key).
 *  2. Explicit `c2s_topic` + `s2c_topic` win verbatim; a missing
 *     `status_topic` is derived by swapping the "/c2s/" segment of the c2s
 *     topic for "/status/".
 *  3. Otherwise `topic_prefix` (leading/trailing '/' trimmed) is used:
 *     - if it already contains "/c2s/", "/s2c/", or "/status/" it names one
 *       topic of the set and the two siblings are derived by segment swap;
 *     - otherwise it is a base prefix: a trailing segment equal to the
 *       access key, MQTT username, or satellite id is dropped, and
 *       `hub_id` is appended unless already present as a path segment.
 *  4. With no prefix at all, `hub_id` yields the base "hivemind/<hub_id>".
 *  5. The topic set is then "<base>/{c2s,s2c,status}/<satellite_id>".
 *
 * Connection endpoint: `mqtt://` is upgraded to `mqtts://` when `tls` is
 * set, and a single trailing '/' is stripped.
 *
 * Status topic conventions (from the Node transport): publish "online"
 * retained at QoS 1 after connecting, register an "offline" retained
 * will, and publish "offline" retained on clean disconnect.
 */
#ifndef THALOVANT_TOPICS_H
#define THALOVANT_TOPICS_H

#include <stdint.h>

#include "thalovant/identity.h"

#define THALOVANT_STATUS_ONLINE "online"
#define THALOVANT_STATUS_OFFLINE "offline"

typedef struct {
    char c2s[THALOVANT_TOPIC_MAX];
    char s2c[THALOVANT_TOPIC_MAX];
    char status[THALOVANT_TOPIC_MAX];
} thalovant_mqtt_topics;

/*
 * Derive the topic set for an identity. Returns THALOVANT_ERR_MISSING when
 * the identity has no MQTT credentials or when neither topic_prefix,
 * hub_id, nor explicit topics are available; THALOVANT_ERR_NOMEM when a
 * derived topic overflows THALOVANT_TOPIC_MAX.
 */
int thalovant_mqtt_topics_derive(const thalovant_identity *identity, thalovant_mqtt_topics *out);

/* Connection URI with the tls scheme upgrade applied (see header comment). */
int thalovant_mqtt_endpoint(const thalovant_mqtt_credentials *mqtt, char *out, size_t cap);

/*
 * MQTT client id: "thalovant-" + access key with every character outside
 * [a-zA-Z0-9_-] replaced by '-', truncated to 48 characters.
 */
int thalovant_mqtt_client_id(const char *access_key, char *out, size_t cap);

typedef struct {
    char scheme[16];
    char host[THALOVANT_MQTT_ENDPOINT_MAX];
    char path[THALOVANT_PATH_MAX]; /* includes any query string; "" if none */
    uint16_t port;                 /* explicit or scheme default */
    bool tls;                      /* mqtts / wss / https / ssl */
} thalovant_endpoint;

/*
 * Split "scheme://host[:port][/path]" (IPv6 hosts in brackets supported).
 * Default ports: mqtt 1883, mqtts 8883, ws/http 80, wss/https 443.
 */
int thalovant_endpoint_parse(const char *url, thalovant_endpoint *out);

#endif /* THALOVANT_TOPICS_H */
