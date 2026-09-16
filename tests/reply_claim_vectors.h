/* Generated from the shared Python reply-claim-vectors.json. */
/* Regenerate with tools/generate-reply-claim-vectors.mjs; do not edit. */
typedef struct { bool handled, failed, claimed; size_t count; const char *stages[4]; } reply_claim_vector;
static const reply_claim_vector REPLY_CLAIM_VECTORS[] = {
    {true, false, true, 0, {NULL}},
    {true, false, true, 0, {NULL}},
    {false, false, false, 0, {NULL}},
    {true, true, false, 1, {"intent"}},
    {true, false, true, 1, {"ovos-padatious-pipeline-plugin"}},
    {true, false, false, 1, {"ovos-fallback-pipeline-plugin"}},
    {true, false, true, 2, {"ovos-fallback-pipeline-plugin", "ovos-converse-pipeline-plugin"}},
    {true, false, true, 2, {"z-stage", "a-stage"}},
    {true, false, true, 0, {NULL}},
    {true, false, true, 0, {NULL}},
    {true, false, false, 1, {"fallback"}},
    {true, false, true, 2, {"FALLBACK", "fallback"}},
    {true, false, false, 1, {"prefix-fallback-suffix"}},
    {true, false, true, 1, {" stage "}},
    {true, false, true, 1, {"段階"}},
    {true, true, false, 0, {NULL}},
};
