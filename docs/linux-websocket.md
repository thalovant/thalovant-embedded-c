# Linux + libwebsockets integration

This sketch shows how a Linux SBC satellite wires the library to
libwebsockets (an `esp_websocket_client` port is analogous). Documentation
only — the snippets are illustrative and not compiled in this repository.

## 1. Identity and connection URL

```c
#include "thalovant/thalovant.h"

thalovant_identity identity;
thalovant_identity_parse(identity_json, identity_len, &identity);

uint8_t key[16];
thalovant_crypto_runtime_key(identity.crypto_key, key);

/* WSS endpoint: from the identity's data-plane endpoints, or
 * default_master when it is already a ws(s):// URL. */
char authorization[256];
thalovant_wire_authorization("ThalovantEmbeddedC/0.1.1", identity.access_key,
                             authorization, sizeof(authorization));

char url[512];
thalovant_wire_ws_url(wss_endpoint, authorization, url, sizeof(url));

thalovant_endpoint parsed;
thalovant_endpoint_parse(url, &parsed);

struct lws_client_connect_info info = {
    .context = context,
    .address = parsed.host,
    .port = parsed.port,
    .path = parsed.path,        /* includes ?authorization=... */
    .host = parsed.host,
    .ssl_connection = parsed.tls ? LCCSCF_USE_SSL : 0,
    .protocol = "hivemind",
};
lws_client_connect_via_info(&info);
```

## 2. Handshake

After the socket opens, the hub sends a plaintext `handshake`/`shake`
frame. Only the preshared-key mode is supported: answer with a *plaintext*
hello, after which every frame is sealed.

```c
static bool handshake_complete = false;

static void on_ws_text(const char *msg, size_t len)
{
    if (thalovant_wire_is_encrypted(msg, len)) {
        uint8_t plain[4096];
        size_t plain_len;
        if (thalovant_envelope_decrypt_json(key, msg, len, plain,
                                            sizeof(plain), &plain_len)
                != THALOVANT_OK) {
            return;
        }
        handle_frame((const char *)plain, plain_len);
        return;
    }
    handle_frame(msg, len);
}

static void handle_frame(const char *json, size_t len)
{
    thalovant_wire_frame frame;
    if (thalovant_wire_parse(json, len, &frame) != THALOVANT_OK) {
        return;
    }
    if (thalovant_wire_is_preshared_handshake(&frame)) {
        char hello[512];
        int hello_len = thalovant_wire_hello(identity.public_key,
                                             "thalovant-c-<uuid>",
                                             identity.site_id,
                                             hello, sizeof(hello));
        ws_send_text(hello, (size_t)hello_len); /* plaintext, by design */
        handshake_complete = true;
        return;
    }
    if (handshake_complete && strcmp(frame.msg_type, "bus") == 0) {
        thalovant_ask_event event;
        thalovant_ask_classify(json, len, current_request_id, &event);
        /* drive the ask state machine — see docs/esp32-mqtt.md §5 */
    }
}
```

## 3. Sending sealed frames

On WSS the encrypted envelope is the **JSON** form
`{"ciphertext","tag","nonce"}` with hex fields and a 16-byte nonce:

```c
static void send_frame(char *frame_json, size_t frame_len)
{
    if (!handshake_complete) {
        ws_send_text(frame_json, frame_len);
        return;
    }
    uint8_t nonce[THALOVANT_GCM_NONCE_LEN];
    getrandom(nonce, sizeof(nonce), 0);

    char envelope[8192];
    int envelope_len = thalovant_envelope_encrypt_json(
        key, nonce, (uint8_t *)frame_json, frame_len,
        envelope, sizeof(envelope));
    ws_send_text(envelope, (size_t)envelope_len);
}

/* an utterance */
char frame[1024];
thalovant_ask_request ask = { "turn on the lights", "en-us",
                              session_id, identity.site_id, request_id };
int frame_len = thalovant_ask_build_frame(&ask, frame, sizeof(frame));
send_frame(frame, (size_t)frame_len);
```

Note `thalovant_envelope_encrypt_json` seals in place: `frame_json` holds
ciphertext bytes afterwards — rebuild or copy it if you need to resend.

## 4. Lifecycle notes

- **Keepalive**: libwebsockets ping/pong suffices; the hub does not require
  application-level pings.
- **Reconnect**: rebuild the URL (the authorization value is static per
  identity) and redo the handshake; a new `hello` starts a new session.
- **Incoming `query`/`cascade` frames**: parse with
  `thalovant_wire_parse`, correlate on `metadata.query_id`, and unwrap
  `payload` (itself a HiveMessage) — the classifier accepts the inner bus
  frame JSON as-is.
