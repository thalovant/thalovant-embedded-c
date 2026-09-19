/* Generated from the shared Python refusal-vectors.json. */
/* Regenerate with tools/generate-refusal-vectors.mjs; do not edit. */
#define REFUSAL_GRACE_SECONDS 10

/* A refusal, as the hub sends it, beside what every SDK must read out of it. */
typedef struct {
  const char *name;
  const char *frame;
  const char *denied_type;
  const char *code;
  const char *reason;
  bool has_quota;
  const char *period;
  long limit, used, reset_after;
  size_t allowed_count;
  const char *allowed[2];
} refusal_vector;
static const refusal_vector REFUSAL_VECTORS[] = {
    {"an allow-list refusal names the type and carries no quota", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"acl_disallowed_type\",\"reason\":\"recognizer_loop:utterance not in allowed_types\",\"data\":{\"allowed\":[\"speak\",\"ovos.skills.fallback.list\"]}},\"context\":{\"source\":\"hivemind-core\",\"destination\":\"sat-1\"}}}", "recognizer_loop:utterance", "acl_disallowed_type", "recognizer_loop:utterance not in allowed_types", false, "", 0, 0, 0, 2, {"speak", "ovos.skills.fallback.list"}},
    {"a spent daily quota carries the numbers behind it", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"intent_quota_exceeded\",\"reason\":\"daily intent quota exceeded\",\"data\":{\"period\":\"daily\",\"limit\":50,\"used\":50,\"reset_after\":36120,\"request_type\":\"recognizer_loop:utterance\"}},\"context\":{\"source\":\"hivemind-core\",\"destination\":\"sat-1\"}}}", "recognizer_loop:utterance", "intent_quota_exceeded", "daily intent quota exceeded", true, "daily", 50, 50, 36120, 0, {NULL, NULL}},
    {"a spent monthly quota is the same shape with its own period", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"intent_quota_exceeded\",\"reason\":\"monthly intent quota exceeded\",\"data\":{\"period\":\"monthly\",\"limit\":1000,\"used\":1000,\"reset_after\":1}},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "intent_quota_exceeded", "monthly intent quota exceeded", true, "monthly", 1000, 1000, 1, 0, {NULL, NULL}},
    {"a quota refusal missing its numbers still reads as a quota, with zeros", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"intent_quota_exceeded\",\"reason\":\"intent quota exceeded\",\"data\":{}},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "intent_quota_exceeded", "intent quota exceeded", true, "", 0, 0, 0, 0, {NULL, NULL}},
    {"negative counts are not counts", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"intent_quota_exceeded\",\"reason\":\"daily intent quota exceeded\",\"data\":{\"period\":\"daily\",\"limit\":-5,\"used\":\"-1\",\"reset_after\":-60}},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "intent_quota_exceeded", "daily intent quota exceeded", true, "daily", 0, 0, 0, 0, {NULL, NULL}},
    {"quota numbers on a refusal that is not a quota are ignored", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"acl_disallowed_type\",\"reason\":\"not allowed\",\"data\":{\"period\":\"daily\",\"limit\":5,\"used\":5,\"reset_after\":60}},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "acl_disallowed_type", "not allowed", false, "", 0, 0, 0, 0, {NULL, NULL}},
    {"a hub whose agent bus is down refuses with its own code", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"backend_unavailable\",\"reason\":\"agent bus is not connected\",\"data\":{}},\"context\":{\"source\":\"hivemind-core\",\"destination\":\"sat-1\"}}}", "recognizer_loop:utterance", "backend_unavailable", "agent bus is not connected", false, "", 0, 0, 0, 0, {NULL, NULL}},
    {"only non-blank strings are message types an operator can allow", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\",\"code\":\"acl_disallowed_type\",\"reason\":\"\",\"data\":{\"allowed\":[\"speak\",3,null,\"  \",\"  mycroft.stop  \",true]}},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "acl_disallowed_type", "", false, "", 0, 0, 0, 2, {"speak", "mycroft.stop"}},
    {"a denial with no nested data at all is still a refusal", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":{\"denied_type\":\"recognizer_loop:utterance\"},\"context\":{\"source\":\"hivemind-core\"}}}", "recognizer_loop:utterance", "", "", false, "", 0, 0, 0, 0, {NULL, NULL}},
};

/* The hub understood and has nothing for it: not a refusal, not a fault. */
typedef struct { const char *name; const char *frame; } unanswered_vector;
static const unanswered_vector UNANSWERED_VECTORS[] = {
    {"an unmatched intent is unanswered, not a failure", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.unmatched\",\"data\":{\"utterance\":\"book me a flight to the moon\",\"lang\":\"en-US\"},\"context\":{\"request_id\":\"req-1\",\"session\":{\"session_id\":\"sat-1\"}}}}"},
    {"the legacy intent failure is the same answer from an older hub", "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"complete_intent_failure\",\"data\":{\"utterance\":\"book me a flight to the moon\"},\"context\":{\"request_id\":\"req-1\"}}}"},
};

/* Whose an uncorrelated denial is, when more than one utterance is out. */
typedef struct {
  const char *name;
  const char *request_id;
  const char *denied_type;
  size_t asks, queries, sends;
  bool taken;
} refusal_correlation_vector;
static const refusal_correlation_vector REFUSAL_CORRELATION_VECTORS[] = {
    {"the only ask in flight takes an uncorrelated refusal of what it sent", NULL, "recognizer_loop:utterance", 1, 0, 0, true},
    {"with two asks in flight neither takes it, because either could be wrong", NULL, "recognizer_loop:utterance", 2, 0, 0, false},
    {"an ask beside a query does not take it either", NULL, "recognizer_loop:utterance", 1, 1, 0, false},
    {"a fire-and-forget utterance sent moments ago could be the one refused", NULL, "recognizer_loop:utterance", 1, 0, 1, false},
    {"a refusal of some other type never fails a good ask", NULL, "ovos.skills.fallback.list", 1, 0, 0, false},
    {"a refusal that does carry this ask's request id is always this ask's", "req-own", "recognizer_loop:utterance", 2, 1, 0, true},
    {"a refusal carrying another ask's request id is never this ask's", "req-other", "recognizer_loop:utterance", 1, 0, 0, false},
};
