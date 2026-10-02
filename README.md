# Thalovant Embedded C Client

[![CI](https://github.com/thalovant/thalovant-embedded-c/actions/workflows/ci.yml/badge.svg)](https://github.com/thalovant/thalovant-embedded-c/actions/workflows/ci.yml)
[![Licence](https://img.shields.io/github/license/thalovant/thalovant-embedded-c)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-docs.thalovant.com-5c6bc0)](https://docs.thalovant.com/developers/sdks/embedded-c/)

Protocol glue for building Thalovant/HiveMind satellite devices in pure C99
— ESP32/ESP-IDF, Zephyr, bare-metal, or Linux SBCs.

The library is transport-agnostic: you bring your own MQTT and/or WebSocket
client and TLS stack, and it provides identity parsing, topics, encryption,
wire framing and the ask and intent helpers. The core paths never allocate,
with zero external dependencies.

## Requirements

A C99 compiler (gcc or clang). Your own MQTT or WebSocket client and TLS stack.

## Install

Vendor the library or fetch it by an immutable release tag (current:
`v0.7.2`):

```sh
# git submodule
git submodule add https://github.com/thalovant/thalovant-embedded-c.git \
    third_party/thalovant-embedded-c
git -C third_party/thalovant-embedded-c checkout v0.7.2
```

CMake `FetchContent`, ESP-IDF component refs and Zephyr west manifests work
the same way with the same tag. To embed it in your own build system, compile
`src/*.c` with `-Iinclude`.

## Quick start

New integrations use the Noise v3 transport. Follow the
[Noise v3 guide](docs/noise-v3.md) for the handshake and the first request, and
the [documentation](https://docs.thalovant.com/developers/sdks/embedded-c/) for
identity parsing, topics, intents and fallback handlers.

## Documentation

| Topic | Where |
| ----- | ----- |
| Overview, what the library provides, what you bring, releases, integration sketch | [Embedded C Library](https://docs.thalovant.com/developers/sdks/embedded-c/) |
| Intent inventory, fallback handlers, request hints, audio, reply claims | [Embedded C Library](https://docs.thalovant.com/developers/sdks/embedded-c/) |
| Everything else | [docs.thalovant.com](https://docs.thalovant.com) |

## Development

```sh
make            # build/libthalovant.a
make test       # host-side, offline test suite
make fuzz       # Clang/libFuzzer, ASan + UBSan; default 60 seconds
make CC=clang test
```

Builds warning-free with `-Wall -Wextra -Werror -pedantic` on gcc and clang.
Every GitHub release carries a source archive, a CycloneDX SBOM and a
`SHA256SUMS` file; the archive and SBOM are attested with GitHub Actions
provenance:

```sh
gh attestation verify thalovant-embedded-c-<version>.tar.gz \
    --repo thalovant/thalovant-embedded-c
```

## Security

See [SECURITY.md](https://github.com/thalovant/.github/blob/main/SECURITY.md).

## Licence

MIT — see [LICENSE](LICENSE).
