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

/* One bus frame per context, so the skill ids are read the way a client reads
   them -- through thalovant_ask_event_skill_id -- rather than transcribed. */
typedef struct {
  size_t frames;
  const char *frame[3];
  size_t count;
  const char *skills[2];
} reply_skill_vector;
static const reply_skill_vector REPLY_SKILL_VECTORS[] = {
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\"}}}"}, 0, {NULL}},
    {0, {NULL}, 0, {NULL}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\"}}}"}, 0, {NULL}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"intent\"}}}"}, 0, {NULL}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"ovos-padatious-pipeline-plugin\",\"skill_id\":\"weather\"}}}"}, 1, {"weather"}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"ovos-fallback-pipeline-plugin\",\"skill_id\":\"fallback.skill\"}}}"}, 1, {"fallback.skill"}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"ovos-fallback-pipeline-plugin\",\"skill_id\":\"first\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"ovos-converse-pipeline-plugin\",\"skill_id\":\"second\"}}}"}, 2, {"first", "second"}},
    {3, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"z-stage\",\"skill_id\":\"z-skill\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"a-stage\",\"skill_id\":\"a-skill\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"z-stage\",\"skill_id\":\"z-skill\"}}}"}, 2, {"z-skill", "a-skill"}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"\",\"skill_id\":\"\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":null,\"skill_id\":null}}}"}, 0, {NULL}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":true,\"skill_id\":123}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":[\"fallback\"],\"skill_id\":{\"id\":\"x\"}}}}"}, 0, {NULL}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":123}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"fallback\",\"skill_id\":\"real\"}}}"}, 1, {"real"}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"FALLBACK\",\"skill_id\":\"Skill\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"fallback\",\"skill_id\":\"skill\"}}}"}, 2, {"Skill", "skill"}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"prefix-fallback-suffix\"}}}"}, 0, {NULL}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\" stage \",\"skill_id\":\" skill \"}}}"}, 1, {" skill "}},
    {2, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"段階\",\"skill_id\":\"技能\"}}}", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\",\"pipeline_id\":\"段階\",\"skill_id\":\"技能\"}}}"}, 1, {"技能"}},
    {1, {"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},\"context\":{\"request_id\":\"r\"}}}"}, 0, {NULL}},
};
