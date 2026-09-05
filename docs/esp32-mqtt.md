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
         "src/intents.c"
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
Every builder and codec here reports what it wrote, or a negative
`thalovant_err`; check it before you send, or a buffer too small publishes
whatever the buffer happened to hold.

```c
static void publish_message(const thalovant_hive_message *msg)
{
    uint8_t frame[1024];
    size_t frame_len;
    if (thalovant_wire_encode_binary(msg, frame, sizeof(frame), &frame_len)
            != THALOVANT_OK) {
        return;                 /* THALOVANT_ERR_NOMEM: frame[] too small */
    }

    uint8_t nonce[THALOVANT_GCM_NONCE_LEN];
    esp_fill_random(nonce, sizeof(nonce));

    uint8_t sealed[sizeof(frame) + 32];
    size_t sealed_len;
    if (thalovant_envelope_encrypt_binary(s_key, nonce, frame, frame_len,
                                          sealed, sizeof(sealed), &sealed_len)
            != THALOVANT_OK) {
        return;
    }
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
int frame_len = thalovant_ask_build_frame(&ask, frame_json, sizeof(frame_json));
if (frame_len < 0) {
    return;                     /* THALOVANT_ERR_NOMEM: nothing to send */
}
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

## 6. What can be said: the intent inventory

A satellite can ask its hub what it can be asked — every intent each skill
registered for a language, and the sentences that reach it as the skill's
locale files wrote them (`what is the weather in {location}`) — over its
own session, with no control-plane credential. The queries are bus frames
built like the ask frame and sealed the same way; the replies come back on
`s_topics.outbound` and are correlated by the request id.

One query at a time is enough for a satellite: keep its request id and
whether it has been answered together, and start both when you send it.

```c
/* the state of the inventory query in flight */
static struct {
    char request_id[THALOVANT_REQUEST_ID_MAX];
    bool answered;                       /* the hub delivers replies twice */
} s_inventory;

/* 1. ovos.intent.list for one language */
static void ask_what_can_be_said(void)
{
    snprintf(s_inventory.request_id, sizeof(s_inventory.request_id),
             "thalovant-request-%08x", (unsigned)esp_random());
    s_inventory.answered = false;        /* per request, never once per boot */

    thalovant_intent_list_request list = {
        .lang = "en-us",
        .session_id = session_id,
        .site_id = s_identity.site_id,
        .request_id = s_inventory.request_id,
        .include_definitions = true,     /* a runtime may attach the sentences */
    };
    if (thalovant_intent_list_build_frame(&list, frame_json,
                                          sizeof(frame_json)) < 0) {
        /* THALOVANT_ERR_NOMEM: frame_json[] is too small for this query.
         * Nothing was sent, so leave no request outstanding. */
        s_inventory.request_id[0] = '\0';
        return;
    }
    /* wrap with encode_binary + encrypt_binary and publish, as for ask */
}
```

For each decoded `bus` frame, hand the JSON to the classifier. The frame
buffer must outlive the event: the rows are read from it, one at a time,
through a callback — nothing is copied out of the frame but the row being
delivered, so a manifest of hundreds of intents costs one
`thalovant_intent_registration` of stack.

```c
static bool on_allowed_type(const thalovant_intent_allowed_type *allowed, void *user)
{
    /* allowed->type: one message type this connection may publish */
    return true;
}

static bool on_sample(const thalovant_intent_sample *sample, void *user)
{
    /* sample->text; sample->has_slot says it carries a {slot} placeholder,
     * so a whole sentence is the better one to show on a display */
    return true;
}

static bool on_row(const thalovant_intent_registration *row, void *user)
{
    /* row->skill_id, row->intent_name, row->lang, row->engine
     * (THALOVANT_INTENT_ENGINE_PADATIOUS for template intents,
     * _ADAPT for keyword ones), row->enabled */
    if (row->definition_json != NULL) {
        /* include_definitions was honoured: the sentences are here */
        thalovant_intent_samples(row->definition_json, row->definition_len,
                                 on_sample, user);
    } else if (row->engine == THALOVANT_INTENT_ENGINE_PADATIOUS) {
        /* remember (skill_id, intent_name, lang) to describe below */
    }
    return true;                        /* false stops the walk early */
}

thalovant_intent_event reply;
if (thalovant_intent_classify(json, json_len, s_inventory.request_id, &reply)
        != THALOVANT_OK) {
    return;                             /* malformed frame */
}
switch (reply.kind) {
case THALOVANT_INTENT_LIST_RESPONSE:
    if (s_inventory.answered) break;    /* a repeat of the reply we took */
    s_inventory.answered = true;
    /* Returns the row count, or a negative error: THALOVANT_ERR_HUB_REFUSED
     * when the listing itself said ok:false (reply.error carries the hub's
     * words) -- a refused listing is not an empty hub, and showing it as no
     * intents would tell a person the device can do nothing -- and a walk
     * that meets malformed JSON stops there and says so. */
    if (thalovant_intent_list_rows(&reply, on_row, NULL) < 0) {
        /* say the listing failed; do not draw an empty inventory */
    }
    break;
case THALOVANT_INTENT_POLICY_DENIED:
    /* This connection may not publish reply.denied_type: give up now
     * rather than waiting out the timeout. thalovant_intent_allowed_types()
     * walks the types it may publish (non-empty string entries only,
     * trimmed: a number, a null, or a blank there is not a message type);
     * the dashboard's connection settings fix it. */
    thalovant_intent_allowed_types(&reply, on_allowed_type, NULL);
    break;
default:
    break;                              /* THALOVANT_INTENT_IGNORE */
}
```

When the runtime did not attach definitions, describe the template intents
row by row. Send every `ovos.intent.describe` at once (one request id each)
and match the replies by request id — or, for a hub that does not echo the
id, by the definition's own `skill_id`/`intent_name`/`lang`
(`thalovant_intent_same_language` folds `fr-FR`/`fr_fr`). A describe that
never arrives simply leaves that intent without sentences.

```c
thalovant_intent_describe_request describe = {
    .skill_id = "thalovant-skill-weather.thalovant",
    .intent_name = "current.weather",
    .lang = "en-us",
    .session_id = session_id,
    .site_id = s_identity.site_id,
    .request_id = "req-desc-1",
};
if (thalovant_intent_describe_build_frame(&describe, frame_json,
                                          sizeof(frame_json)) < 0) {
    return;                     /* THALOVANT_ERR_NOMEM: nothing to send */
}

/* the reply */
static bool on_definition(const thalovant_intent_definition *definition, void *user)
{
    if (definition->engine == THALOVANT_INTENT_ENGINE_PADATIOUS) {
        thalovant_intent_samples(definition->definition_json,
                                 definition->definition_len, on_sample, user);
    }
    return true;
}

thalovant_intent_classify(json, json_len, "req-desc-1", &reply);
if (reply.kind == THALOVANT_INTENT_DESCRIBE_RESPONSE) {
    /* reply.ok is false for an unknown registration (reply.error) */
    thalovant_intent_definitions(&reply, on_definition, NULL);
}
```

The walkers return `THALOVANT_ERR_POLICY_DENIED` when handed a denial, and
`THALOVANT_ERR_NOMEM` when a field exceeds its `THALOVANT_INTENT_*_MAX`
(see `include/thalovant/config.h`; nothing is truncated). Use the same 5 s
per-query deadline the desktop SDKs use.

A negative answer means different things on the two queries. A listing that
says `ok: false` failed and told you nothing, so `thalovant_intent_list_rows`
returns `THALOVANT_ERR_HUB_REFUSED`; a describe that says `ok: false` is the
hub answering that it does not know that registration, so
`thalovant_intent_definitions` delivers nothing and the intent keeps its row
without sentences.

The connection's allow-list, in the dashboard's connection settings, needs
`ovos.intent.list` to read the manifest at all. `ovos.intent.describe` is
needed only if you go on to ask for the sentences behind a registration —
a satellite that only lists what can be said never sends it.
