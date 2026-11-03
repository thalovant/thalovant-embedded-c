Last Edit: Claude Opus 5.5 - 2026-11-03 - Motive: Record the CodeRabbit follow-ups to #24 and #26 (test harness and refusal-vector generator).

# Maintenance Report

## 2026-11-03 — CodeRabbit follow-ups (#24, #26)

Two review threads left unresolved on merged pull requests still applied to `main` (8167740). Both are in the test harness; the library itself is unchanged.

| Old PR | Finding | Change |
| --- | --- | --- |
| #26 | The refusal-vector generator passed every number through `JSON.parse`/`JSON.stringify`, so a frame carried a double's spelling rather than the shared file's. | `tools/generate-refusal-vectors.mjs` keeps each number's lexeme from `tests/fixtures/refusal-vectors.json`. The case "a number no policy could have meant is not a count" now sends `"used":1e+20`, as the shared vectors do, instead of `100000000000000000000`; `tests/refusal_vectors.h` is regenerated. |
| #24 | `tests/test_ask.c` held the first-seen skill ids in a fixed `seen[8]`, and read `v->skills[count]` after a failed `CHECK(count < v->count)`. | `seen` takes the generated `skills[]` width, and the loop stops at the first id past `v->count`. |

Verification:

- `make test` (gcc) and `make test CC=clang CFLAGS="-O1 -g -fsanitize=address,undefined ..."`: 18362 checks, 0 failures; `make vectors-strict` passes.
- Reverting the generator makes `node tools/generate-refusal-vectors.mjs --check` fail ("out of date with the shared vectors").
- With a temporary vector naming nine skills, the old `tests/test_ask.c` fails (`count == 8, expected 9`) and the new one passes. With a temporary vector whose frames name one skill more than it expects, the old test reads past `skills[]` (AddressSanitizer: global-buffer-overflow) and the new one reports the failed check and stops. Neither temporary vector is committed.
- `check-sdk-parity.py --consumer thalovant-embedded-c` verifies after refreshing the three evidence hashes in `contracts/sdk-parity.json`.

## Transparency Report

- **AI Model**: Claude Opus 5.5.
- **Actions Taken**: Verified each unresolved CodeRabbit thread against current `main`; rewrote the number handling in `tools/generate-refusal-vectors.mjs`; regenerated `tests/refusal_vectors.h`; changed the skill-id loop in `tests/test_ask.c`; refreshed parity evidence hashes; ran the suites above, including the revert and mutation checks.
- **Oversight**: Changes go through pull-request CI (gcc, clang, sanitizer, fuzz, 32-bit, parity). No human reviewed this change before the pull request was opened.
