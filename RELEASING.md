# Releasing the Thalovant embedded C library

The library is not published to a package registry. A release is an
immutable `v<version>` git tag plus a GitHub release carrying a validated,
attested source archive — integrators vendor the tree or fetch it by tag
(git submodule, CMake `FetchContent`, ESP-IDF component ref, Zephyr west
manifest, or the release tarball).

## Prerequisites

None. No repository secrets are required: releases use the default
`GITHUB_TOKEN`, and the provenance/SBOM attestations use the workflow's
OIDC `id-token` — there is nothing to provision or rotate.

## Version sources that move together

The canonical version is `THALOVANT_VERSION` in
`include/thalovant/version.h`. These must carry the same version in a
release, and `tests/test_version.c` fails the suite when they drift:

- `VERSION` (repository root)
- `#define THALOVANT_VERSION` in `include/thalovant/version.h`
  (`THALOVANT_EMBEDDED_C_VERSION` in `include/thalovant/thalovant.h` is an
  alias of it)
- `#define TLV_EXPECTED_VERSION` in `tests/test_version.c`
- the `v<version>` references in `README.md`
- the `ThalovantEmbeddedC/<version>` example in `docs/linux-websocket.md`
- the topmost `CHANGELOG.md` section

## Release flow

1. For a deliberate release, update every file listed above to the new
   version (plus a real `CHANGELOG.md` entry) and merge to `main`.
2. The **Auto Release** workflow (`auto-release.yml`) runs on every push to
   `main`:
   - It exits without releasing when nothing release-relevant (`src/`,
     `include/`, `tests/`, `Makefile`, `VERSION`, `docs/`, `README.md`,
     `LICENSE`) changed since the latest `v*` tag.
   - If the current `VERSION` is untagged, it releases it as-is.
   - If the current `VERSION` is already tagged, it auto-bumps a patch
     version across all the files listed above, prepends a `CHANGELOG.md`
     section, and commits and pushes the bump.
   - It then runs `make test` with both gcc and clang, creates the
     `v<version>` tag and GitHub release, and dispatches the **Release**
     workflow.
3. The **Release** workflow (`release.yml`) checks out the tag, verifies
   `tag == v$(cat VERSION)` and that the header define matches, re-runs
   `make test` under gcc and clang, then:
   - builds a deterministic source archive with `git archive`
     (`thalovant-embedded-c-<version>.tar.gz`);
   - generates a minimal CycloneDX JSON SBOM (the library has zero
     dependencies) whose `serialNumber` is a `urn:uuid` derived from the
     archive's SHA-256, so re-runs are reproducible;
   - attests provenance and the SBOM for the archive with
     `actions/attest`;
   - attaches the archive, the SBOM, and a `SHA256SUMS` file to the GitHub
     release, preserving any assets already attached.

A release run can also be started manually: **Actions → Release → Run
workflow** with the immutable `release_tag` (for example `v0.1.0`).

## What integrators consume

Pin a tag, never a branch:

```sh
git -C third_party/thalovant-embedded-c checkout v0.1.0   # submodule
# or CMake: GIT_TAG v0.1.0
# or ESP-IDF component / Zephyr west manifest pinned to the tag
```

Anyone consuming the release tarball can verify what CI produced:

```sh
sha256sum -c SHA256SUMS
gh attestation verify thalovant-embedded-c-<version>.tar.gz \
    --repo thalovant/thalovant-embedded-c
```

The `git archive` output is reproducible for a given tag (with a matching
git version), so a vendored tree can be re-derived and compared against
the attested archive.

## Rollback

Tags and release assets are immutable once consumers may have resolved
them: never move, delete, or re-tag an existing `v*` tag.

1. Do not attempt to remove or overwrite a broken version.
2. Publish a corrected patch release with aligned `VERSION`, header
   define, test pin, README references, and changelog.
3. Update the integration docs and compatibility notes to name the
   replacement version.
