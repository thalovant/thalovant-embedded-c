Last Edit: Claude Sonnet 5.5 - 2026-11-12 - Motive: Record the Python revision and review of every acknowledged reference digest, and the digest a0722fec2c54.

# Maintenance Report

## 2026-11-12: Python reference digests acknowledged on the intercom branch

`contracts/sdk-parity.json` lists three reference digests that thalovant-python-sdk `feature/intercom-v1` (PR 114) published while it moved (this library recorded the `intercom` capability as `not-applicable` when the second of the Python revisions below was current, so it holds three of the five). Each is the digest of the `reference` object of that repository's `contracts/sdk-parity.json` (`scripts/check-sdk-parity.py`, `digest(reference)`), and each was reviewed by reading the Python diff of the `intercom` capability for impact on this library, then confirmed with `check-sdk-parity.py --workspace <checkouts> --consumer thalovant-embedded-c`. Embedded C is a caller-buffer wire library without a control-plane HTTP client, so the capability stays `not-applicable` (the reason is in the file) and every acknowledgement is a review, not a port:

| Digest | Python commit | What moved in the Python reference | Review here |
|---|---|---|---|
| `a82b669bc2cb` | `375b822` | amendment 2: `cancel_announcement`, safe ids, the announce timeout, key replay docs | read; none; no file here changed |
| `69badc4fb072` | `577cb4b` | the final API: `intercom:urgent`, speaker `kind`, `can_play`, `policy_synced`, typed problem codes, one text rule | read; none; no file here changed |
| `a0722fec2c54` | `cc33f67` | review round 2: a requests-style session read off the event loop, the urgent-scope code before the generic scope refusal, nested problem members, docs aligned with the API | read; none; no file here changed |

No source, test or version file changed, so no release is made from this change.

Transparency Report: AI model Claude Sonnet 5.5; actions: added the digest `a0722fec2c54` to `contracts/sdk-parity.json` and recorded the revision and review of every added digest in this report; oversight: none yet, review pending.

## 2026-11-03 — Chat apps parity acknowledgment

**What changed:** `contracts/sdk-parity.json` records the Python reference digest `1e56099da5db` (thalovant-python-sdk#108) and answers its new `chat-apps` capability as not applicable, with the reason the reference gives:

> Embedded C is a caller-buffer wire library without a control-plane HTTP client; the caller owns every request it would make.

No source, test or version file changes, so `auto-release.yml` publishes nothing.

**Why:** the reference's release gate refuses to publish while any consumer has not answered a capability. Chat apps (the `/v1/bridges` owner routes) ship in the Python, Node, Go and Rust SDKs and the MCP server.

**Verification:** `check-sdk-parity.py --workspace <checkouts> --consumer thalovant-embedded-c` verifies this acceptance against the reference branch, and against `main` (where the new digest is only a candidate). The language suite runs in hosted CI on this PR.

### Transparency Report

- **AI Model:** Claude Opus 5.5 (claude-opus-5-5), as a Claude Code agent.
- **Actions Taken:** added the digest and the `chat-apps` entry; wrote this entry and the quick facts, FAQ and suggestions files.
- **Oversight:** hosted CI and the CodeRabbit review gate the merge; the platform owner's coordinating agent merges.

## 2026-11-03 — CodeRabbit follow-ups (#24, #26)

Two review threads left unresolved on merged pull requests still applied to `main` (8167740). Both are in the test harness; the library itself is unchanged.

| Old PR | Finding | Change |
| --- | --- | --- |
| #26 | The refusal-vector generator passed every number through `JSON.parse`/`JSON.stringify`, so a frame carried a double's spelling rather than the shared file's. | `tools/generate-refusal-vectors.mjs` keeps each number's lexeme from `tests/fixtures/refusal-vectors.json`. The case "a number no policy could have meant is not a count" now sends `"used":1e+20`, as the shared vectors do, instead of `100000000000000000000`; `tests/refusal_vectors.h` is regenerated. |
| #24 | `tests/test_ask.c` held the first-seen skill ids in a fixed `seen[8]`, and read `v->skills[count]` after a failed `CHECK(count < v->count)`. | `seen` takes the generated `skills[]` width, and the loop stops at the first id past `v->count`. |

Verification:

- `make test` (gcc) and `make test CC=clang CFLAGS="-O1 -g -fsanitize=address,undefined ..."`: 18364 checks, 0 failures; `make vectors-strict` passes.
- Reverting the generator makes `node tools/generate-refusal-vectors.mjs --check` fail ("out of date with the shared vectors"). Reverting it and regenerating the header makes `make test` fail at the new `test_the_frames_spell_numbers_as_the_shared_file_does` in `tests/test_refusal.c` (`"used":1e+20` is missing from the frame). That check was added at CodeRabbit's suggestion on #36, together with checking the generator's marker guard against the decoded fixture values rather than the raw text.
- With a temporary vector naming nine skills, the old `tests/test_ask.c` fails (`count == 8, expected 9`) and the new one passes. With a temporary vector whose frames name one skill more than it expects, the old test reads past `skills[]` (AddressSanitizer: global-buffer-overflow) and the new one reports the failed check and stops. Neither temporary vector is committed.
- `check-sdk-parity.py --consumer thalovant-embedded-c` verifies after refreshing the three evidence hashes in `contracts/sdk-parity.json`.

## Transparency Report

- **AI Model**: Claude Opus 5.5.
- **Actions Taken**: Verified each unresolved CodeRabbit thread against current `main`; rewrote the number handling in `tools/generate-refusal-vectors.mjs`; regenerated `tests/refusal_vectors.h`; changed the skill-id loop in `tests/test_ask.c`; refreshed parity evidence hashes; ran the suites above, including the revert and mutation checks.
- **Oversight**: Changes go through pull-request CI (gcc, clang, sanitizer, fuzz, 32-bit, parity). No human reviewed this change before the pull request was opened.
