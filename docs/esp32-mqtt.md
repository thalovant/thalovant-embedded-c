# ESP32 (ESP-IDF) + esp-mqtt integration

This sketch shows how a satellite built on ESP-IDF wires the library to
`esp-mqtt`. It is documentation only — the snippets are illustrative and
not compiled in this repository.

## Component setup

Copy (or submodule) this repository into `components/thalovant/` and add a
minimal `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "src/json.c" "src/codec.c" "src/sha256.c" "src/aes_gcm.c"
         "src/identity.c" "src/topics.c" "src/wire.c" "src/ask.c"
    INCLUDE_DIRS "include")
```

## 1. Load the identity

Store the identity JSON (from device provisioning /
`ClientIdentifyResource`) in NVS or a SPIFFS file:

```c
#include "thalovant/thalovant.h"

static thalovant_identity s_identity;
static uint8_t s_key[16];
static thalovant_mqtt_topics s_topics;

void satellite_init(const char *identity_json, size_t len)
{
    ESP_ERROR_CHECK(thalovant_identity_parse(identity_json, len, &s_identity)
                    == THALOVANT_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(thalovant_crypto_runtime_key(s_identity.crypto_key, s_key)
                    == THALOVANT_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(thalovant_mqtt_topics_derive(&s_identity, &s_topics)
                    == THALOVANT_OK ? ESP_OK : ESP_FAIL);
}
```

## 2. Connect esp-mqtt

```c
char endpoint[THALOVANT_MQTT_ENDPOINT_MAX];
thalovant_mqtt_endpoint(&s_identity.mqtt, endpoint, sizeof(endpoint));

char client_id[64];
thalovant_mqtt_client_id(s_identity.access_key, client_id, sizeof(client_id));

esp_mqtt_client_config_t config = {
    .broker.address.uri = endpoint,           /* mqtt[s]://host:port */
    .credentials.username = s_identity.mqtt.username,
    .credentials.authentication.password = s_identity.mqtt.password,
    .credentials.client_id = client_id,
    .session.keepalive = 60,
    .session.last_will = {
        .topic = s_topics.status,
        .msg = THALOVANT_STATUS_OFFLINE,
        .qos = 1,
        .retain = true,
    },
    /* .broker.verification.certificate = ... when s_identity.mqtt.tls */
};
esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
```

On `MQTT_EVENT_CONNECTED`:

1. `esp_mqtt_client_subscribe(client, s_topics.outbound, s_identity.mqtt.qos)`
2. Publish `THALOVANT_STATUS_ONLINE` on `s_topics.status` (qos 1, retain)
3. Send the plaintext hello (see below) sealed as a binary envelope.

## 3. Send frames (binary envelope)

The MQTT transport uses the HiveMind **binary** framing: a binary frame
(`thalovant_wire_encode_binary`) sealed as `nonce || ciphertext || tag`
(`thalovant_envelope_encrypt_binary`). Nonces come from the hardware RNG.

```c
static void publish_message(const thalovant_hive_message *msg)
{
    uint8_t frame[1024];
    size_t frame_len;
    thalovant_wire_encode_binary(msg, frame, sizeof(frame), &frame_len);

    uint8_t nonce[THALOVANT_GCM_NONCE_LEN];
    esp_fill_random(nonce, sizeof(nonce));

    uint8_t sealed[sizeof(frame) + 32];
    size_t sealed_len;
    thalovant_envelope_encrypt_binary(s_key, nonce, frame, frame_len,
                                      sealed, sizeof(sealed), &sealed_len);
    esp_mqtt_client_publish(client, s_topics.inbound, (const char *)sealed,
                            (int)sealed_len, s_identity.mqtt.qos, 0);
}

static void send_hello(void)
{
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"pubkey\":\"\",\"session\":{\"session_id\":\"thalovant-esp32-%08x\"},"
             "\"site_id\":\"%s\"}",
             (unsigned)esp_random(), s_identity.site_id);
    thalovant_hive_message hello = { "hello", payload, NULL, NULL,
                                     NULL, NULL, NULL, NULL };
    publish_message(&hello);
}
```

## 4. Receive frames

On `MQTT_EVENT_DATA` (topic `s_topics.outbound`), payloads are encrypted binary
envelopes; decode with the inverse pipeline:

```c
static void on_mqtt_data(const uint8_t *data, size_t len)
{
    uint8_t plain[1024];
    size_t plain_len;
    if (thalovant_envelope_decrypt_binary(s_key, data, len, plain,
                                          sizeof(plain), &plain_len)
            != THALOVANT_OK) {
        return; /* not for us / tampered */
    }
    char metadata[256], payload[768];
    thalovant_wire_binary_frame frame;
    if (thalovant_wire_decode_binary(plain, plain_len, metadata,
                                     sizeof(metadata), payload,
                                     sizeof(payload), &frame)
            != THALOVANT_OK) {
        return;
    }
    /* frame.msg_type: "shake" -> answer with send_hello() (preshared key
     * handshake); "bus" -> feed the JSON to the ask classifier. */
}
```

## 5. The ask loop

```c
char frame_json[1024];
thalovant_ask_request ask = {
    .text = "what time is it",
    .lang = "en-us",
    .session_id = session_id,   /* stable per conversation */
    .site_id = s_identity.site_id,
    .request_id = request_id,   /* unique per ask */
};
thalovant_ask_build_frame(&ask, frame_json, sizeof(frame_json));
/* wrap frame_json with encode_binary + encrypt_binary and publish. */
```

For each decoded `bus` frame, rebuild the JSON
(`{"msg_type":"bus","payload":<payload>}` from the binary decode) or use
the payload directly, then:

```c
thalovant_ask_event event;
thalovant_ask_classify(json, json_len, request_id, &event);
switch (event.kind) {
case THALOVANT_ASK_SPEAK:          /* collect event.text (normalize +
                                      dedupe consecutive duplicates) */
case THALOVANT_ASK_HANDLED:        /* hub done routing; short grace timer
                                      for a late speak */
case THALOVANT_ASK_POLICY_DENIED:
case THALOVANT_ASK_QUERY_TIMEOUT:  /* terminal failure */
case THALOVANT_ASK_INTENT_FAILURE: /* record, but keep waiting */
default: break;                    /* THALOVANT_ASK_IGNORE */
}
```

Timeouts (mirroring the Node SDK defaults): 12 s overall, then a 5 s
empty-reply wait after `HANDLED`, and a 250 ms settle window after the
first speak fragment.
