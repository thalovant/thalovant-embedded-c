# Changelog

## 0.1.2 - 2026-08-15

- Automated patch release of the unreleased changes on `main` since v0.1.1.

## Unreleased

- Harden `thalovant_mqtt_topics_derive` topic_prefix validation to match the
  Node/Go SDKs: after the surrounding slashes are trimmed the prefix is also
  whitespace-trimmed, an empty result returns `THALOVANT_ERR_MISSING`, and a
  prefix carrying an MQTT wildcard (`#`/`+`) or a control character returns
  `THALOVANT_ERR_INVALID`.
- Add derived-topic boundary coverage: a max-length prefix derives without
  truncation, and a dedicated reduced-`THALOVANT_TOPIC_MAX` test build
  exercises the `THALOVANT_ERR_NOMEM` guard so a `<prefix>/status` that would
  overflow `THALOVANT_TOPIC_MAX` is rejected rather than silently truncated.

## 0.1.1 - 2026-08-15

- Automated patch release of the unreleased changes on `main` since v0.1.0.

## 0.1.0 - 2026-08-13

Initial release.

- Identity JSON parser (`thalovant_identity`) with the Node/Go SDK field
  aliases and the API `ClientIdentifyResource` shape, including MQTT broker
  credentials.
- MQTT topic derivation (`thalovant_topics`) matching the Node SDK's
  `mqttTopicsForIdentity` byte-for-byte, plus connection endpoint/port
  parsing and client-id derivation.
- Self-contained AES-128-GCM (`thalovant_aes_gcm`) supporting 16-byte
  HiveMind nonces and 12-byte legacy nonces, with constant-time tag
  comparison; validated against NIST vectors and Node-generated
  known-answer vectors.
- SHA-256 (for hashed topic satellite ids), hex, and Base64 codecs.
- HiveMessage wire envelope (`thalovant_wire`): frame serialize/parse with
  explicit nulls, `{ciphertext,tag,nonce}` JSON envelope with hex/Base64
  auto-detect, binary MQTT envelope, binary frame codec, WSS authorization
  builder, and preshared-key handshake/hello helpers.
- Ask helpers (`thalovant_ask`): `recognizer_loop:utterance` frame builders
  with request/session correlation and a reply-frame classifier.
- In-repo JSON tokenizer; no dynamic allocation in core paths; no external
  dependencies.
- Canonical library version: `THALOVANT_VERSION` in
  `include/thalovant/version.h`, mirrored by the root `VERSION` file and
  pinned by the test suite.
- Host-side offline test suite (`make test`) green under gcc and clang with
  `-Wall -Wextra -Werror`.
