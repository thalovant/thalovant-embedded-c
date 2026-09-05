# Changelog

## 0.3.0 - 2026-09-05

- `thalovant_intent_list_rows` returns the new `THALOVANT_ERR_HUB_REFUSED`
  for an `ovos.intent.list.response` that says `{"ok": false, "error": ...}`,
  where it used to return the same zero rows an empty manifest returns. A
  refused listing has told us nothing, and walking it as no intents showed a
  person a device that can do nothing; the hub's own words are in
  `thalovant_intent_event.error`. `thalovant_intent_definitions` keeps
  returning 0 for a describe that says `ok: false`, which is a real answer —
  the hub does not know that registration, so the intent simply has no
  sentences. Reported by the Kotlin port's review (thalovant-python-sdk#34).
- Add `thalovant_intent_allowed_types`, the walker for the message types a
  `hive.policy.denied` says the connection may publish. The list carries
  only non-empty string entries, trimmed: a number or a null there is not a
  message type, and neither is `""` or `"  "`, and naming one would send an
  operator reading which types to allow after `"3"`, `"null"`, or nothing at
  all. `thalovant_intent_event.allowed_count` now counts those entries
  rather than every JSON element, and a list naming no type — or one too
  malformed to walk — leaves `allowed_json` NULL with the denial still
  standing on `denied_type`, `code` and `reason`. Reported by the Kotlin
  port's review (thalovant-python-sdk#34) and its follow-up on the blanks
  (thalovant-python-sdk#35).
- Document what a connection's allow-list must hold for the inventory:
  `ovos.intent.list` to read the manifest, and `ovos.intent.describe` only
  when it goes on to ask for the sentences behind a registration.

## 0.2.0 - 2026-09-05

- Add the intent inventory (`thalovant_intents`): a satellite asks its hub
  what can be said over its own session, with no control-plane credential,
  from the runtime's intent manifest (OVOS-INTENT-4 §10).
  `thalovant_intent_list_build_payload`/`_frame` send `ovos.intent.list` for
  a language (`include_definitions` asks the runtime to attach each row's
  definition); `thalovant_intent_describe_build_payload`/`_frame` send
  `ovos.intent.describe` for one registration. Both carry the request id
  and language in the context the way the ask frame does, so the hub echoes
  them back.
- Add `thalovant_intent_classify`, the reply classifier alongside
  `thalovant_ask_classify`: `ovos.intent.list.response`,
  `ovos.intent.describe.response` and `hive.policy.denied`, correlated by
  `context.request_id`. A reply carrying another request's id is ignored; one
  carrying none is delivered with an empty `request_id` so a describe can be
  matched by the definition's own `skill_id`/`intent_name`/`lang`, as the
  contract asks of a hub that does not echo the id. A bare bus payload (the
  binary frame decoder's output) is accepted as well as the full frame.
- Rows, definitions and sample sentences stream through callbacks into
  caller-owned structs — `thalovant_intent_list_rows`,
  `thalovant_intent_definitions`, `thalovant_intent_samples` — so a manifest
  of any size is walked in bounded memory; no aggregate is built and nothing
  allocates. `method` `template`/`keyword` map to engines
  `THALOVANT_INTENT_ENGINE_PADATIOUS`/`_ADAPT`; a sample flags whether it
  carries a `{slot}`. `thalovant_intent_same_language` compares tags
  case-insensitively with `_`/`-` folded (`fr-fr` is answered as `fr-FR`).
- Recognise `hive.policy.denied` as its own outcome: the event carries
  `denied_type`, `code` (`THALOVANT_POLICY_CODE_ACL_DISALLOWED_TYPE`),
  `reason` and the raw `allowed` list, and the walkers return the new
  `THALOVANT_ERR_POLICY_DENIED`, so a refused query surfaces at once instead
  of waiting out a timeout.
- Add the event-name constants (`THALOVANT_EVENT_INTENT_LIST`,
  `..._LIST_RESPONSE`, `..._DESCRIBE`, `..._DESCRIBE_RESPONSE`,
  `THALOVANT_EVENT_POLICY_DENIED`, and the engines' manifest names
  `THALOVANT_EVENT_ADAPT_MANIFEST[_GET]`/`THALOVANT_EVENT_PADATIOUS_MANIFEST[_GET]`).
  The names-only engine-manifest fallback of the desktop SDKs is not driven
  by this library; the constants let an integrator send it as a plain bus
  frame.
- Add shallow JSON scans to `thalovant_json`: `thalovant_json_scan`,
  `thalovant_json_scan_key` and `thalovant_json_scan_next` read one value at
  a time without a token pool (the tokenizer's string and primitive grammar,
  shared), for payloads larger than `THALOVANT_WIRE_MAX_TOKENS` can hold.
  Walking a container validates its separators — exactly one comma between
  members, none before the closing bracket — so malformed input is refused
  with `THALOVANT_ERR_JSON` instead of being handed to a caller.
- New `config.h` limits: `THALOVANT_LANG_MAX`, `THALOVANT_EVENT_NAME_MAX`,
  `THALOVANT_INTENT_SKILL_ID_MAX`, `THALOVANT_INTENT_NAME_MAX`,
  `THALOVANT_INTENT_METHOD_MAX`, `THALOVANT_INTENT_SESSION_ID_MAX`,
  `THALOVANT_INTENT_SAMPLE_MAX`, `THALOVANT_INTENT_ERROR_MAX`,
  `THALOVANT_POLICY_CODE_MAX`, `THALOVANT_POLICY_REASON_MAX`. A field over
  its limit is refused with `THALOVANT_ERR_NOMEM`, never truncated.

## 0.1.3 - 2026-08-31

- Classify the OVOS `ovos.intent.unmatched` bus event as an ask intent
  failure (`THALOVANT_ASK_INTENT_FAILURE`), matching the sibling SDKs. OVOS
  renamed Mycroft's `complete_intent_failure`; the legacy name is still
  accepted for older runtimes. Fixes an utterance that matches no intent
  waiting out the full ask timeout instead of surfacing the failure (#22).

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
