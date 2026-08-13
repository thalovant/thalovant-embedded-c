# Repository instructions

This repository owns the embedded C client library for supported Thalovant
public API and HiveMind runtime contracts. The protocol source of truth is
the Node SDK (`../thalovant-node-sdk/src/transport-mqtt.ts`,
`transport-core.ts`, `crypto.ts`, `wire.ts`) and the API schemas
(`../thalovant-api/app/schemas/clients.py`). Read the platform contracts in
`../infra-manifests/docs/thalovant-platform/` when available.

Rules:

- Pure C99. No dynamic allocation in core paths: callers provide every
  buffer; any helper that allocates must live in a clearly-marked optional
  layer.
- Keep zero external dependencies and zero vendored third-party code: the
  JSON tokenizer, AES-128-GCM, SHA-256, and codecs are written in-tree.
- The library stays transport-agnostic: never link or depend on an MQTT,
  WebSocket, or TLS implementation. Integration examples live in `docs/`
  as documentation only.
- Wire bytes are contract: serialized frames, derived topics, and sealed
  envelopes must remain byte-compatible with the Node SDK. Cover every
  observable change with fixtures captured from the reference SDK (see the
  fixture provenance comments in `tests/`).
- Crypto changes require re-validating the NIST vectors and regenerating
  the Node known-answer vectors with a local `node` binary.
- Builds must stay warning-free under `-Wall -Wextra -Werror -pedantic` on
  both gcc and clang; `make test` must pass with both compilers and the
  test suite must stay network-free.
- Update types, implementation, docs, tests, changelog, and version
  together for observable contract changes.
- Consume additive server behavior only after compatible server support
  exists.
- Never publish credentials, identity files, or generated secrets.
- Do not create a release for internal platform changes with no C SDK
  impact; record `no SDK impact` in the coordinated change instead.

Validate with `make test CC=gcc` and `make test CC=clang`.

Releases are automated: `auto-release.yml` tags and creates the GitHub
release for an untagged `VERSION` on `main` (auto-bumping a patch when the
current version is already tagged), and `release.yml` validates the tag,
re-runs the gcc and clang suites, and attaches the attested source
archive, CycloneDX SBOM, and `SHA256SUMS` to the release. The `VERSION`
file, `THALOVANT_VERSION` in `include/thalovant/version.h`, its pin in
`tests/test_version.c`, the `v<version>` references in `README.md`, the
`ThalovantEmbeddedC/<version>` example in `docs/linux-websocket.md`, and
`CHANGELOG.md` must move together in a release. See `RELEASING.md` for
the flow, integrator pinning, and rollback rules.

Rollback by tagging a corrected patch release; never move or delete an
existing tag that consumers may already resolve.
