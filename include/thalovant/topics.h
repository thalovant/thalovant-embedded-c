/*
 * MQTT topic derivation for HiveMind satellites.
 *
 * The identity's `topic_prefix` is the full base for the device, plaintext
 * (no hashing), of the form "hivemind/<hub-id>/<access-key>". Each channel
 * is a plain suffix appended to it (leading/trailing '/' trimmed first):
 *   - inbound  "<topic_prefix>/in"     — publish requests to the hub
 *   - outbound "<topic_prefix>/out"    — subscribe for the hub's replies
 *   - status   "<topic_prefix>/status" — retained presence / LWT
 *
 * Connection endpoint: `mqtt://` is upgraded to `mqtts://` when `tls` is
 * set, and a single trailing '/' is stripped.
 *
 * Status topic conventions: publish "online" retained at QoS 1 after
 * connecting, register an "offline" retained will, and publish "offline"
 * retained on clean disconnect.
 */
#ifndef THALOVANT_TOPICS_H
#define THALOVANT_TOPICS_H

#include <stdint.h>

#include "thalovant/identity.h"

#define THALOVANT_STATUS_ONLINE "online"
#define THALOVANT_STATUS_OFFLINE "offline"

typedef struct {
    char inbound[THALOVANT_TOPIC_MAX];
    char outbound[THALOVANT_TOPIC_MAX];
    char status[THALOVANT_TOPIC_MAX];
} thalovant_mqtt_topics;

/*
 * Derive the topic set for an identity. Returns THALOVANT_ERR_MISSING when
 * the identity has no MQTT credentials or an empty topic_prefix;
 * THALOVANT_ERR_NOMEM when a derived topic overflows THALOVANT_TOPIC_MAX.
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
