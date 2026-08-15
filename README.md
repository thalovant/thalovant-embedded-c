# Thalovant Embedded C Client

Protocol glue for building Thalovant/HiveMind satellite devices in pure C99
— ESP32/ESP-IDF, Zephyr, bare-metal, or Linux SBCs.

This library is **transport-agnostic**: you bring your own MQTT client
(esp-mqtt, Eclipse Paho embedded, ...) and/or WebSocket client
(libwebsockets, esp_websocket_client, ...). The library provides everything
protocol-specific:

- **`thalovant_identity`** — parser for the identity JSON issued by the
  Thalovant API (access key, crypto key, site id, hub endpoints, MQTT
  broker credentials), accepting the same field aliases as the Node and Go
  SDKs.
- **`thalovant_topics`** — MQTT in/out/status topic derivation from the
  identity's `topic_prefix` (trimmed and validated: wildcards and control
  characters are rejected, oversized topics never silently truncate), plus
  connection endpoint/port parsing and client-id derivation.
- **`thalovant_aes_gcm`** — self-contained AES-128-GCM (16-byte HiveMind
  nonces and 12-byte legacy nonces) with constant-time tag verification,
  validated against NIST vectors and known-answer vectors generated with
  Node's `crypto` module.
- **`thalovant_wire`** — HiveMessage frame build/parse (explicit nulls,
  byte-comparable with the Node SDK), the `{ciphertext,tag,nonce}` JSON
  envelope with hex/Base64 auto-detection, the binary MQTT envelope and
  binary frame codec, the WSS `authorization` query-parameter builder, and
  the preshared-key handshake/hello helpers.
- **`thalovant_ask`** — builders for `recognizer_loop:utterance` frames with
  request/session correlation, and a classifier for the reply frames
  (speak / handled / complete_intent_failure / policy denied / query
  timeout) so you can run the ask loop on your own event loop.
- A small in-repo JSON tokenizer (`thalovant_json`) and hex/Base64 codecs —
  **zero external dependencies, zero third-party code**.

The core paths never allocate: every function writes into caller-provided
buffers, with all size limits tunable through `include/thalovant/config.h`
defines.

## What the integrator brings

| You provide                   | The library provides                       |
| ----------------------------- | ------------------------------------------ |
| MQTT and/or WebSocket client  | topics, endpoints, frame/envelope codecs   |
| TLS stack                     | `tls` flag + scheme/port parsing           |
| Random number generator       | (nonces are always caller-supplied)        |
| Event loop / timers           | frame classifier + ask-loop semantics      |
| Identity JSON storage         | identity parser                            |

## Quick sketch (MQTT)

```c
#include "thalovant/thalovant.h"

thalovant_identity identity;
thalovant_identity_parse(identity_json, identity_len, &identity);

thalovant_mqtt_topics topics;
thalovant_mqtt_topics_derive(&identity, &topics);

uint8_t key[16];
thalovant_crypto_runtime_key(identity.crypto_key, key);

/* connect your MQTT client to the derived endpoint...            */
/* subscribe topics.outbound; publish "online" retained on topics.status */

/* send an utterance */
char frame[1024];
thalovant_ask_request ask = { "what time is it", "en-us",
                              "sess-1", identity.site_id, "req-1" };
thalovant_ask_build_frame(&ask, frame, sizeof(frame));

uint8_t nonce[16];   /* fill from your RNG — never reuse */
uint8_t sealed[1100];
size_t sealed_len;
thalovant_envelope_encrypt_binary(key, nonce, (uint8_t *)frame,
                                  strlen(frame), sealed, sizeof(sealed),
                                  &sealed_len);
/* publish sealed on topics.inbound ... */

/* classify replies arriving on topics.outbound */
thalovant_ask_event event;
thalovant_ask_classify(plaintext, plaintext_len, "req-1", &event);
if (event.kind == THALOVANT_ASK_SPEAK) { /* speak event.text */ }
```

Full walkthroughs: [docs/esp32-mqtt.md](docs/esp32-mqtt.md) and
[docs/linux-websocket.md](docs/linux-websocket.md).

## Getting a release

Integrators vendor the library or fetch it by an immutable release tag
(current: `v0.1.1`) — as a git submodule, via CMake `FetchContent`, as an
ESP-IDF component ref, or in a Zephyr west manifest:

```sh
# git submodule
git submodule add https://github.com/thalovant/thalovant-embedded-c.git \
    third_party/thalovant-embedded-c
git -C third_party/thalovant-embedded-c checkout v0.1.1
```

```cmake
# CMake FetchContent
FetchContent_Declare(thalovant
  GIT_REPOSITORY https://github.com/thalovant/thalovant-embedded-c.git
  GIT_TAG        v0.1.1)
```

Every GitHub release also carries a reproducible source archive
(`thalovant-embedded-c-<version>.tar.gz`), a CycloneDX SBOM, and a
`SHA256SUMS` file. The archive and SBOM are attested with GitHub Actions
provenance; verify with:

```sh
gh attestation verify thalovant-embedded-c-<version>.tar.gz \
    --repo thalovant/thalovant-embedded-c
```

## Building

```sh
make            # build/libthalovant.a
make test       # host-side, offline test suite
make CC=clang test
```

Requires only a C99 compiler; builds warning-free with
`-Wall -Wextra -Werror -pedantic` on gcc and clang. To embed in your own
build system, compile `src/*.c` with `-Iinclude`.

## License

MIT — see [LICENSE](LICENSE).
