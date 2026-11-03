Last Edit: Claude Opus 5.5 - 2026-11-03 - Motive: Start the audit log with the CodeRabbit follow-ups to #24 and #26.

# Audit

Known issues, technical debt and their status, from test runs and code reading. Each entry names the code it concerns.

## Fixed

- **Shared refusal vectors lost their number spelling** (CodeRabbit on #26). `tools/generate-refusal-vectors.mjs` built each frame with `JSON.stringify` of a parsed object, so `1e+20` in `tests/fixtures/refusal-vectors.json` reached `tests/test_refusal.c` as `100000000000000000000` and the exponent form was never read through `thalovant_ask_refusal()`. On 2026-09-18 the same path turned `9223372036854775808` into `9223372036854776000`. The generator now keeps each lexeme; `make vectors-strict` fails if the committed header differs. With the lexeme restored, `make test` passes (18362 checks, 0 failures), so the reader already handled the exponent form.
- **Fixed-width `seen[8]` and an out-of-bounds read in the reply-skill check** (CodeRabbit on #24). `tests/test_ask.c` sized `seen` independently of the generated `reply_skill_vector.skills[]` and indexed `v->skills[count]` after a failed `CHECK(count < v->count)`. Shown with temporary vectors: a nine-skill case failed spuriously, and an extra id read past `skills[]` under AddressSanitizer. Both fixed.

## Open

- None found in this pass.
