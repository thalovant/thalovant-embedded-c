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
- **`thalovant_intents`** — the intent inventory: builders for
  `ovos.intent.list` (what the hub can be asked, per language) and
  `ovos.intent.describe` (the sentences behind one intent, `{slot}`
  placeholders included), a classifier for their replies and for
  `hive.policy.denied`, and streaming walkers that deliver each row,
  definition and sample sentence through a callback — a manifest of any
  size in bounded memory, over the satellite's own session with no
  control-plane credential.
- A small in-repo JSON tokenizer (`thalovant_json`) with shallow scans for
  payloads larger than a token pool, and hex/Base64 codecs — **zero
  external dependencies, zero third-party code**.

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
int frame_len = thalovant_ask_build_frame(&ask, frame, sizeof(frame));
if (frame_len < 0) { /* THALOVANT_ERR_NOMEM: frame[] too small; send nothing */ }

uint8_t nonce[16];   /* fill from your RNG — never reuse */
uint8_t sealed[1100];
size_t sealed_len;
thalovant_envelope_encrypt_binary(key, nonce, (uint8_t *)frame,
                                  (size_t)frame_len, sealed, sizeof(sealed),
                                  &sealed_len);
/* publish sealed on topics.inbound ... */

/* classify replies arriving on topics.outbound */
thalovant_ask_event event;
thalovant_ask_classify(plaintext, plaintext_len, "req-1", &event);
if (event.kind == THALOVANT_ASK_SPEAK) { /* speak event.text */ }
```

## What can be said (intent inventory)

```c
/* one query in flight: its id, and whether its reply has been taken.
 * Keep one of these per query -- a shared flag would let one query's
 * reply suppress another's. */
struct inventory_query { const char *request_id; bool answered; };

/* ask the hub's intent manifest for one language */
struct inventory_query query = { "req-2", false };   /* both set together */
thalovant_intent_list_request list = { "en-us", "sess-1", identity.site_id,
                                       query.request_id, false };
if (thalovant_intent_list_build_frame(&list, frame, sizeof(frame)) < 0) {
    /* THALOVANT_ERR_NOMEM: frame[] too small; nothing was built to send */
}
/* seal and publish as above */

/* each reply frame: rows stream through a callback, one at a time */
static bool on_row(const thalovant_intent_registration *row, void *user)
{
    printf("%s:%s (%s)\n", row->skill_id, row->intent_name, row->lang);
    return true;                      /* false stops the walk */
}

thalovant_intent_event reply;
thalovant_intent_classify(plaintext, plaintext_len, query.request_id, &reply);
switch (reply.kind) {
case THALOVANT_INTENT_LIST_RESPONSE:
    if (query.answered) break;        /* the hub delivers every reply twice */
    query.answered = true;
    thalovant_intent_list_rows(&reply, on_row, NULL);   /* count, or < 0 */
    break;
case THALOVANT_INTENT_POLICY_DENIED:  /* reply.denied_type names the query */
    break;
default: break;                       /* THALOVANT_INTENT_IGNORE */
}

/* the sentences behind one intent: ovos.intent.describe, then
 * thalovant_intent_definitions() and thalovant_intent_samples() on the
 * reply -- see docs/esp32-mqtt.md section 6 */
```

Full walkthroughs: [docs/esp32-mqtt.md](docs/esp32-mqtt.md) and
[docs/linux-websocket.md](docs/linux-websocket.md).

## Getting a release

Integrators vendor the library or fetch it by an immutable release tag
(current: `v0.2.0`) — as a git submodule, via CMake `FetchContent`, as an
ESP-IDF component ref, or in a Zephyr west manifest:

```sh
# git submodule
git submodule add https://github.com/thalovant/thalovant-embedded-c.git \
    third_party/thalovant-embedded-c
git -C third_party/thalovant-embedded-c checkout v0.2.0
```

```cmake
# CMake FetchContent
FetchContent_Declare(thalovant
  GIT_REPOSITORY https://github.com/thalovant/thalovant-embedded-c.git
  GIT_TAG        v0.2.0)
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
