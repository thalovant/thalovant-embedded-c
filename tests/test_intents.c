/*
 * The intent inventory against a hub that behaves like the one observed.
 *
 * Reply shapes follow the Python SDK's FakeHubTransport (tests/test_intents.py,
 * copied from a live runtime on 2026-09-05): ovos.intent.list.response rows,
 * ovos.intent.describe.response definitions carrying `samples` as the
 * skill's locale files wrote them, hive.policy.denied for a type the
 * connection may not publish, every reply delivered twice, and a hub that
 * substitutes its own session id while echoing the request id and lang.
 * The query frames are byte-comparable with the ask frame fixture's context
 * layout (tests/test_ask.c).
 */
#include <stdio.h>

#include "harness.h"
#include "thalovant/intents.h"
#include "thalovant/json.h"

#define WEATHER "thalovant-skill-weather.thalovant"
#define SHADOW "thalovant-skill-custos-shadow.thalovant"

/* The hub echoes the request context but answers over its own session. */
#define ECHOED_CONTEXT(req, lang)                                                          \
    "\"context\":{\"request_id\":\"" req "\",\"thalovant_request_id\":\"" req "\",\"lang\":\"" \
    lang "\",\"session\":{\"session_id\":\"71048b7f-e7b0-4360-8fb5-a03816f78617\","         \
    "\"site_id\":\"kitchen\",\"lang\":\"" lang "\",\"request_id\":\"" req "\"}}"

#define WEATHER_ROW(lang)                                                                  \
    "{\"skill_id\":\"" WEATHER "\",\"intent_name\":\"current.weather\",\"lang\":\"" lang    \
    "\",\"method\":\"template\",\"enabled\":true,\"session_id\":\"default\"}"
#define SHADOW_ROW(lang)                                                                   \
    "{\"skill_id\":\"" SHADOW "\",\"intent_name\":\"custos.incidents\",\"lang\":\"" lang    \
    "\",\"method\":\"template\",\"enabled\":true,\"session_id\":\"default\"}"

static const char LIST_RESPONSE_EN[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
    "{\"ok\":true,\"intents\":[" WEATHER_ROW("en-us") "," SHADOW_ROW("en-us") "]},"
    ECHOED_CONTEXT("req-1", "en-us") "},\"metadata\":{},\"route\":[],\"node\":null,"
    "\"target_site_id\":null,\"target_pubkey\":null,\"source_peer\":null}";

/* The runtime standardises what it stores: fr-fr is answered as fr-FR. */
static const char LIST_RESPONSE_FR[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
    "{\"ok\":true,\"intents\":[" WEATHER_ROW("fr-FR") "]}," ECHOED_CONTEXT("req-2", "fr-fr")
    "}}";

static const char POLICY_DENIED[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
    "{\"denied_type\":\"ovos.intent.list\",\"code\":\"acl_disallowed_type\",\"reason\":"
    "\"ovos.intent.list not in allowed_types\",\"data\":{\"msg_type\":\"ovos.intent.list\","
    "\"allowed\":[\"recognizer_loop:utterance\",\"speak\"]}}," ECHOED_CONTEXT("req-1", "en-us")
    "}}";

#define WEATHER_DEFINITION(lang)                                                           \
    "{\"skill_id\":\"" WEATHER "\",\"intent_name\":\"current.weather\",\"lang\":\"" lang    \
    "\",\"samples\":[\"what is the weather\",\"what is the weather in {location}\","         \
    "\"how is it outside\"],\"blacklist\":[],\"slot_blacklist\":{}}"

static const char DESCRIBE_RESPONSE[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
    "{\"ok\":true,\"definitions\":[{\"method\":\"template\",\"definition\":"
    WEATHER_DEFINITION("en-us") "}]}," ECHOED_CONTEXT("req-d1", "en-us") "}}";

static const char DESCRIBE_UNKNOWN[] =
    "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
    "{\"ok\":false,\"error\":\"unknown intent\"}," ECHOED_CONTEXT("req-d2", "en-us") "}}";

/* ------------------------------------------------------------- helpers */

static int classify(const char *frame, const char *request_id, thalovant_intent_event *event)
{
    return thalovant_intent_classify(frame, strlen(frame), request_id, event);
}

typedef struct {
    int rows;
    int stop_after;
    thalovant_intent_registration first;
    thalovant_intent_registration last;
} row_log;

static bool log_row(const thalovant_intent_registration *row, void *user)
{
    row_log *log = user;
    if (log->rows == 0) {
        log->first = *row;
    }
    log->last = *row;
    log->rows++;
    return log->stop_after == 0 || log->rows < log->stop_after;
}

typedef struct {
    int count;
    thalovant_intent_definition first;
} definition_log;

static bool log_definition(const thalovant_intent_definition *definition, void *user)
{
    definition_log *log = user;
    if (log->count == 0) {
        log->first = *definition;
    }
    log->count++;
    return true;
}

typedef struct {
    int count;
    int with_slot;
    char joined[512];
} sample_log;

static bool log_sample(const thalovant_intent_sample *sample, void *user)
{
    sample_log *log = user;
    CHECK_INT_EQ(sample->index, log->count);
    log->count++;
    if (sample->has_slot) {
        log->with_slot++;
    }
    if (log->joined[0] != '\0') {
        strcat(log->joined, " | ");
    }
    strcat(log->joined, sample->text);
    return true;
}

/* ------------------------------------------------------------- queries */

static void test_build_list_query(void)
{
    thalovant_intent_list_request request = { "en-us", "sess-1", "kitchen", "req-1", false };
    char out[1024];
    int len = thalovant_intent_list_build_payload(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"type\":\"ovos.intent.list\",\"data\":{\"lang\":\"en-us\"},"
                      "\"context\":{\"request_id\":\"req-1\",\"thalovant_request_id\":\"req-1\","
                      "\"lang\":\"en-us\",\"session\":{\"session_id\":\"sess-1\",\"site_id\":"
                      "\"kitchen\",\"lang\":\"en-us\",\"request_id\":\"req-1\"}}}");

    /* include_definitions rides in data; the frame wraps the payload the
     * way the ask frame does. */
    request.include_definitions = true;
    request.site_id = NULL;
    request.lang = NULL;
    len = thalovant_intent_list_build_frame(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list\",\"data\":"
                      "{\"lang\":\"en-us\",\"include_definitions\":true},\"context\":"
                      "{\"request_id\":\"req-1\",\"thalovant_request_id\":\"req-1\",\"lang\":"
                      "\"en-us\",\"session\":{\"session_id\":\"sess-1\",\"lang\":\"en-us\","
                      "\"request_id\":\"req-1\"}}},\"metadata\":{},\"route\":[],\"node\":null,"
                      "\"target_site_id\":null,\"target_pubkey\":null,\"source_peer\":null}");

    thalovant_intent_list_request no_request = { "en-us", "sess-1", NULL, NULL, false };
    CHECK_INT_EQ(thalovant_intent_list_build_payload(&no_request, out, sizeof(out)),
                 THALOVANT_ERR_INVALID);
    thalovant_intent_list_request no_session = { "en-us", NULL, NULL, "req-1", false };
    CHECK_INT_EQ(thalovant_intent_list_build_payload(&no_session, out, sizeof(out)),
                 THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_list_build_payload(&request, out, 32), THALOVANT_ERR_NOMEM);
}

static void test_build_describe_query(void)
{
    thalovant_intent_describe_request request = { WEATHER, "current.weather", "fr-fr",
                                                  "sess-1", NULL, "req-d1" };
    char out[1024];
    int len = thalovant_intent_describe_build_payload(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"type\":\"ovos.intent.describe\",\"data\":{\"skill_id\":\"" WEATHER
                      "\",\"intent_name\":\"current.weather\",\"lang\":\"fr-fr\"},\"context\":"
                      "{\"request_id\":\"req-d1\",\"thalovant_request_id\":\"req-d1\",\"lang\":"
                      "\"fr-fr\",\"session\":{\"session_id\":\"sess-1\",\"lang\":\"fr-fr\","
                      "\"request_id\":\"req-d1\"}}}");
    len = thalovant_intent_describe_build_frame(&request, out, sizeof(out));
    CHECK(len > 0);
    CHECK(strncmp(out, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe\"",
                  56) == 0);

    thalovant_intent_describe_request no_intent = { WEATHER, "", "en-us", "sess-1", NULL, "r" };
    CHECK_INT_EQ(thalovant_intent_describe_build_payload(&no_intent, out, sizeof(out)),
                 THALOVANT_ERR_INVALID);
    thalovant_intent_describe_request no_skill = { NULL, "x", "en-us", "sess-1", NULL, "r" };
    CHECK_INT_EQ(thalovant_intent_describe_build_payload(&no_skill, out, sizeof(out)),
                 THALOVANT_ERR_INVALID);
}

/* ------------------------------------------------------------- replies */

static void test_list_response_rows(void)
{
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(LIST_RESPONSE_EN, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    CHECK(event.ok);
    CHECK_STR_EQ(event.error, "");
    CHECK_STR_EQ(event.request_id, "req-1");
    CHECK_STR_EQ(event.lang, "en-us");
    CHECK_INT_EQ(event.count, 2);
    CHECK(event.items_json != NULL && event.items_json[0] == '[');
    CHECK_INT_EQ(event.items_json[event.items_len - 1], ']');

    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 2);
    CHECK_INT_EQ(log.rows, 2);
    CHECK_STR_EQ(log.first.skill_id, WEATHER);
    CHECK_STR_EQ(log.first.intent_name, "current.weather");
    CHECK_STR_EQ(log.first.lang, "en-us");
    CHECK_STR_EQ(log.first.method, "template");
    CHECK_INT_EQ(log.first.engine, THALOVANT_INTENT_ENGINE_PADATIOUS);
    CHECK(log.first.enabled);
    CHECK_STR_EQ(log.first.session_id, "default");
    CHECK(log.first.definition_json == NULL);
    CHECK_STR_EQ(log.last.skill_id, SHADOW);
    CHECK_STR_EQ(log.last.intent_name, "custos.incidents");

    /* The callback stops the walk. */
    row_log two = { 0 };
    two.stop_after = 1;
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &two), 1);

    /* Without a required correlation the same reply is still classified,
     * and a bare bus payload (the binary frame decoder's output) works too. */
    CHECK_INT_EQ(classify(LIST_RESPONSE_EN, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    CHECK_STR_EQ(event.request_id, "req-1");
    const char *bare =
        "{\"type\":\"ovos.intent.list.response\",\"data\":{\"ok\":true,\"intents\":["
        WEATHER_ROW("en-us") "]}," ECHOED_CONTEXT("req-1", "en-us") "}";
    CHECK_INT_EQ(classify(bare, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    CHECK_INT_EQ(event.count, 1);
}

static void test_list_response_shapes(void)
{
    /* keyword rows map to adapt; enabled:false is kept; a missing
     * session_id reads "default"; a row naming no intent is skipped, and an
     * unknown method is reported as such rather than guessed. */
    const char *frame =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":true,\"intents\":[{\"skill_id\":\"" SHADOW "\",\"intent_name\":\"custos.status\","
        "\"lang\":\"en-us\",\"method\":\"keyword\",\"enabled\":false},{\"skill_id\":\"\","
        "\"intent_name\":\"orphan\"},{\"intent_name\":\"orphan2\"},{\"skill_id\":\"s\","
        "\"intent_name\":\"n\",\"method\":\"regex\"}]}," ECHOED_CONTEXT("req-3", "en-us") "}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-3", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.count, 4);
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 2);
    CHECK_INT_EQ(log.first.engine, THALOVANT_INTENT_ENGINE_ADAPT);
    CHECK(!log.first.enabled);
    CHECK_STR_EQ(log.first.session_id, "default");
    CHECK_INT_EQ(log.last.engine, THALOVANT_INTENT_ENGINE_UNKNOWN);
    CHECK_STR_EQ(log.last.method, "regex");
    CHECK(log.last.enabled);

    /* An empty manifest is a successful, empty answer: no rows, no error. */
    const char *empty =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":true,\"intents\":[]}," ECHOED_CONTEXT("req-3", "en-us") "}}";
    CHECK_INT_EQ(classify(empty, "req-3", &event), THALOVANT_OK);
    CHECK(event.ok);
    CHECK_INT_EQ(event.count, 0);
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 0);
}

static void test_a_refused_listing_is_not_an_empty_hub(void)
{
    /* ok:false on a listing means the query failed and told us nothing.
     * Walking it as zero rows would show a person a hub that can do
     * nothing, so the walk says so and the hub's words come with it. */
    const char *refused =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":false,\"error\":\"manifest unavailable\"}," ECHOED_CONTEXT("req-3", "en-us")
        "}}";
    thalovant_intent_event event;
    row_log log = { 0 };
    CHECK_INT_EQ(classify(refused, "req-3", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    CHECK(!event.ok);
    CHECK_STR_EQ(event.error, "manifest unavailable");
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_HUB_REFUSED);
    CHECK_INT_EQ(log.rows, 0);
    CHECK_STR_EQ(thalovant_err_str(THALOVANT_ERR_HUB_REFUSED), "the hub refused the query");

    /* A refusal that names no error still fails the listing. */
    const char *silent =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":false}," ECHOED_CONTEXT("req-3", "en-us") "}}";
    CHECK_INT_EQ(classify(silent, "req-3", &event), THALOVANT_OK);
    CHECK_STR_EQ(event.error, "");
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_HUB_REFUSED);

    /* Rows sent alongside ok:false are not a half-answer to walk. */
    const char *refused_with_rows =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":false,\"error\":\"partial\",\"intents\":[" WEATHER_ROW("en-us") "]},"
        ECHOED_CONTEXT("req-3", "en-us") "}}";
    CHECK_INT_EQ(classify(refused_with_rows, "req-3", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.count, 1);
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_HUB_REFUSED);
    CHECK_INT_EQ(log.rows, 0);

    /* The other half of the rule: a describe answering ok:false is a real
     * answer -- the hub does not know that registration -- and leaves the
     * intent without sentences rather than failing the inventory. */
    definition_log definitions = { 0 };
    CHECK_INT_EQ(classify(DESCRIBE_UNKNOWN, "req-d2", &event), THALOVANT_OK);
    CHECK(!event.ok);
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &definitions), 0);
}

/* The integrator's state for one query: the first reply wins. */
typedef struct {
    const char *request_id;
    bool answered;
    int rows;
} pending_query;

static bool count_row(const thalovant_intent_registration *row, void *user)
{
    (void)row;
    ((pending_query *)user)->rows++;
    return true;
}

static void on_frame(pending_query *query, const char *frame)
{
    thalovant_intent_event event;
    if (thalovant_intent_classify(frame, strlen(frame), query->request_id, &event) != THALOVANT_OK ||
        event.kind != THALOVANT_INTENT_LIST_RESPONSE || query->answered) {
        return;
    }
    query->answered = true;
    thalovant_intent_list_rows(&event, count_row, query);
}

static void test_duplicate_delivery_takes_the_first(void)
{
    /* The hub delivers every reply twice; the rows are counted once. */
    pending_query query = { "req-1", false, 0 };
    on_frame(&query, LIST_RESPONSE_EN);
    on_frame(&query, LIST_RESPONSE_EN);
    CHECK(query.answered);
    CHECK_INT_EQ(query.rows, 2);
    /* Another language's reply belongs to another request id. */
    on_frame(&query, LIST_RESPONSE_FR);
    CHECK_INT_EQ(query.rows, 2);
    pending_query french = { "req-2", false, 0 };
    on_frame(&french, LIST_RESPONSE_FR);
    on_frame(&french, LIST_RESPONSE_FR);
    CHECK_INT_EQ(french.rows, 1);
}

static void test_language_tags_fold(void)
{
    /* Asked fr-fr, answered fr-FR: the same language. */
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(LIST_RESPONSE_FR, "req-2", &event), THALOVANT_OK);
    CHECK_STR_EQ(event.lang, "fr-fr");
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 1);
    CHECK_STR_EQ(log.first.lang, "fr-FR");
    CHECK(thalovant_intent_same_language(log.first.lang, "fr-fr"));
    CHECK(thalovant_intent_same_language("fr_FR", " fr-fr "));
    CHECK(thalovant_intent_same_language("EN-US", "en_us"));
    CHECK(!thalovant_intent_same_language("en-us", "en-gb"));
    CHECK(!thalovant_intent_same_language("en", "en-us"));
    CHECK(thalovant_intent_same_language(NULL, ""));
    CHECK(!thalovant_intent_same_language(NULL, "en"));
}

static void test_policy_denied(void)
{
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(POLICY_DENIED, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_POLICY_DENIED);
    CHECK(!event.ok);
    CHECK_STR_EQ(event.denied_type, THALOVANT_EVENT_INTENT_LIST);
    CHECK_STR_EQ(event.code, THALOVANT_POLICY_CODE_ACL_DISALLOWED_TYPE);
    CHECK_STR_EQ(event.reason, "ovos.intent.list not in allowed_types");
    CHECK_STR_EQ(event.request_id, "req-1");
    CHECK_INT_EQ(event.allowed_count, 2);
    CHECK(event.allowed_json != NULL);
    CHECK(strncmp(event.allowed_json, "[\"recognizer_loop:utterance\",\"speak\"]",
                  event.allowed_len) == 0);
    /* The allowed list is walkable with the shallow scanner. */
    thalovant_json_tok allowed = { THALOVANT_JSON_ARRAY, 0, (int)event.allowed_len,
                                   event.allowed_count, -1 };
    size_t cursor = 0;
    thalovant_json_tok elem;
    CHECK_INT_EQ(thalovant_json_scan_next(event.allowed_json, &allowed, &cursor, &elem), 1);
    CHECK(thalovant_json_str_eq(event.allowed_json, &elem, "recognizer_loop:utterance"));

    /* The walkers name the refusal with its own error code. */
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_POLICY_DENIED);
    definition_log definitions = { 0 };
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &definitions),
                 THALOVANT_ERR_POLICY_DENIED);
    CHECK_STR_EQ(thalovant_err_str(THALOVANT_ERR_POLICY_DENIED),
                 "message type refused by the hub's policy");

    /* A denial the hub sends without echoing the request context is still
     * surfaced (its denied_type says what it is about); one carrying another
     * request's id is not ours. */
    const char *anonymous =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
        "{\"denied_type\":\"ovos.intent.describe\",\"code\":\"acl_disallowed_type\","
        "\"reason\":\"ovos.intent.describe not in allowed_types\"}}}";
    CHECK_INT_EQ(classify(anonymous, "req-d1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_POLICY_DENIED);
    CHECK_STR_EQ(event.denied_type, THALOVANT_EVENT_INTENT_DESCRIBE);
    CHECK_STR_EQ(event.request_id, "");
    CHECK(event.allowed_json == NULL);
    CHECK_INT_EQ(event.allowed_count, 0);
    CHECK_INT_EQ(classify(POLICY_DENIED, "req-9", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
}

typedef struct {
    int count;
    char joined[256];
} allowed_log;

static bool log_allowed(const thalovant_intent_allowed_type *allowed, void *user)
{
    allowed_log *log = user;
    CHECK_INT_EQ(allowed->index, log->count);
    log->count++;
    if (log->joined[0] != '\0') {
        strcat(log->joined, " | ");
    }
    strcat(log->joined, allowed->type);
    return true;
}

static void test_the_allowed_list_holds_only_message_types(void)
{
    /* A number or a null in `allowed` is not a message type. Handing one to
     * an operator reading which types to allow would send them after "3". */
    const char *denied =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
        "{\"denied_type\":\"ovos.intent.list\",\"code\":\"acl_disallowed_type\",\"reason\":"
        "\"ovos.intent.list not in allowed_types\",\"data\":{\"msg_type\":\"ovos.intent.list\","
        "\"allowed\":[\"speak\",3,null,{\"type\":\"speak\"},[\"speak\"],true,\"\",\"   \","
        "\"\\t \",\"  recognizer_loop:utterance  \"]}}," ECHOED_CONTEXT("req-1", "en-us") "}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(denied, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_POLICY_DENIED);
    /* The count is the number of message types, not of JSON elements: the
     * blank entries are no more a type than the number is, and the rest
     * come back trimmed. */
    CHECK_INT_EQ(event.allowed_count, 2);
    allowed_log log = { 0, "" };
    CHECK_INT_EQ(thalovant_intent_allowed_types(&event, log_allowed, &log), 2);
    CHECK_STR_EQ(log.joined, "speak | recognizer_loop:utterance");

    /* A list naming no type at all is no allow-list; the denial still says
     * what was refused, which is what the operator acts on. */
    const char *nothing_usable =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
        "{\"denied_type\":\"ovos.intent.list\",\"code\":\"acl_disallowed_type\","
        "\"data\":{\"allowed\":[3,null,\"\",\"  \"]}}," ECHOED_CONTEXT("req-1", "en-us") "}}";
    CHECK_INT_EQ(classify(nothing_usable, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_POLICY_DENIED);
    CHECK_STR_EQ(event.denied_type, THALOVANT_EVENT_INTENT_LIST);
    CHECK(event.allowed_json == NULL);
    CHECK_INT_EQ(event.allowed_count, 0);
    allowed_log none = { 0, "" };
    CHECK_INT_EQ(thalovant_intent_allowed_types(&event, log_allowed, &none), 0);
    CHECK_INT_EQ(none.count, 0);

    /* A list too malformed to walk is dropped rather than handed on half
     * read, and the denial survives it. */
    const char *malformed =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\",\"data\":"
        "{\"denied_type\":\"ovos.intent.list\",\"data\":{\"allowed\":[\"speak\",]}},"
        ECHOED_CONTEXT("req-1", "en-us") "}}";
    CHECK_INT_EQ(classify(malformed, "req-1", &event), THALOVANT_OK);
    CHECK_STR_EQ(event.denied_type, THALOVANT_EVENT_INTENT_LIST);
    CHECK(event.allowed_json == NULL);
    CHECK_INT_EQ(event.allowed_count, 0);

    /* A type longer than THALOVANT_EVENT_NAME_MAX is not truncated. */
    static char long_type[THALOVANT_EVENT_NAME_MAX + 8];
    memset(long_type, 'x', sizeof(long_type) - 1);
    long_type[sizeof(long_type) - 1] = '\0';
    static char frame[512];
    sprintf(frame, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"hive.policy.denied\","
                   "\"data\":{\"denied_type\":\"ovos.intent.list\",\"data\":{\"allowed\":"
                   "[\"%s\"]}}}}", long_type);
    CHECK_INT_EQ(classify(frame, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.allowed_count, 1);
    allowed_log overflow = { 0, "" };
    CHECK_INT_EQ(thalovant_intent_allowed_types(&event, log_allowed, &overflow),
                 THALOVANT_ERR_NOMEM);

    /* The walker takes denials only. */
    CHECK_INT_EQ(classify(LIST_RESPONSE_EN, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_intent_allowed_types(&event, log_allowed, &log), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_allowed_types(NULL, log_allowed, &log), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(classify(POLICY_DENIED, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_intent_allowed_types(&event, NULL, &log), THALOVANT_ERR_INVALID);
}

static void test_include_definitions_fast_path(void)
{
    /* A runtime honouring include_definitions attaches each row's
     * definition; the sentences come straight from the listing. */
    const char *frame =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":true,\"intents\":[{\"skill_id\":\"" WEATHER "\",\"intent_name\":"
        "\"current.weather\",\"lang\":\"en-us\",\"method\":\"template\",\"enabled\":true,"
        "\"session_id\":\"default\",\"definition\":" WEATHER_DEFINITION("en-us") "},"
        SHADOW_ROW("en-us") "]}," ECHOED_CONTEXT("req-1", "en-us") "}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-1", &event), THALOVANT_OK);
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 2);
    CHECK(log.first.definition_json != NULL);
    CHECK_INT_EQ(log.first.definition_json[0], '{');
    CHECK(log.last.definition_json == NULL);

    sample_log samples = { 0, 0, "" };
    CHECK_INT_EQ(thalovant_intent_samples(log.first.definition_json, log.first.definition_len,
                                          log_sample, &samples),
                 3);
    CHECK_INT_EQ(samples.count, 3);
    CHECK_INT_EQ(samples.with_slot, 1);
    CHECK_STR_EQ(samples.joined,
                 "what is the weather | what is the weather in {location} | how is it outside");
}

static void test_describe_response(void)
{
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(DESCRIBE_RESPONSE, "req-d1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_DESCRIBE_RESPONSE);
    CHECK(event.ok);
    CHECK_STR_EQ(event.request_id, "req-d1");
    CHECK_INT_EQ(event.count, 1);

    definition_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &log), 1);
    CHECK_STR_EQ(log.first.skill_id, WEATHER);
    CHECK_STR_EQ(log.first.intent_name, "current.weather");
    CHECK_STR_EQ(log.first.lang, "en-us");
    CHECK_STR_EQ(log.first.method, "template");
    CHECK_INT_EQ(log.first.engine, THALOVANT_INTENT_ENGINE_PADATIOUS);
    CHECK_INT_EQ(log.first.sample_count, 3);
    CHECK(log.first.definition_json != NULL);

    sample_log samples = { 0, 0, "" };
    CHECK_INT_EQ(thalovant_intent_samples(log.first.definition_json, log.first.definition_len,
                                          log_sample, &samples),
                 3);
    CHECK_STR_EQ(samples.joined,
                 "what is the weather | what is the weather in {location} | how is it outside");

    /* An unknown registration: ok:false with the hub's error, no definitions. */
    CHECK_INT_EQ(classify(DESCRIBE_UNKNOWN, "req-d2", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_DESCRIBE_RESPONSE);
    CHECK(!event.ok);
    CHECK_STR_EQ(event.error, "unknown intent");
    CHECK(event.items_json == NULL);
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &log), 0);
}

static void test_describe_shapes(void)
{
    /* Keyword definitions come first and carry no samples; the method may
     * live on the definition alone; blank samples are dropped and the rest
     * trimmed; an item without a definition object is skipped. */
    const char *frame =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
        "{\"ok\":true,\"definitions\":[{\"method\":\"keyword\",\"definition\":{\"skill_id\":\""
        SHADOW "\",\"intent_name\":\"custos.status\",\"lang\":\"en-us\",\"required\":"
        "[\"StatusKeyword\"]}},{\"definition\":{\"method\":\"template\",\"skill_id\":\"" SHADOW
        "\",\"intent_name\":\"custos.status\",\"lang\":\"en-us\",\"samples\":[\"  what is the "
        "status \",\"\",42,\"status of {service}\"]}},{\"method\":\"template\"},"
        "{\"method\":\"template\",\"definition\":{\"lang\":\"en-us\"}}]},"
        ECHOED_CONTEXT("req-d3", "en-us") "}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-d3", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.count, 4);
    definition_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &log), 2);
    CHECK_INT_EQ(log.first.engine, THALOVANT_INTENT_ENGINE_ADAPT);
    CHECK_INT_EQ(log.first.sample_count, 0);
    sample_log none = { 0, 0, "" };
    CHECK_INT_EQ(thalovant_intent_samples(log.first.definition_json, log.first.definition_len,
                                          log_sample, &none),
                 0);

    /* The sample walker takes any definition object directly: here the
     * second (template) item's, cut out of the frame by a shallow scan. */
    sample_log samples = { 0, 0, "" };
    const char *definition = strstr(frame, "{\"method\":\"template\",\"skill_id\"");
    CHECK(definition != NULL);
    thalovant_json_tok tok;
    int end = thalovant_json_scan(definition, strlen(definition), 0, &tok);
    CHECK(end > 0);
    CHECK_INT_EQ(thalovant_intent_samples(definition, (size_t)end, log_sample, &samples), 2);
    CHECK_STR_EQ(samples.joined, "what is the status | status of {service}");
    CHECK_INT_EQ(samples.with_slot, 1);
}

static void test_hub_that_does_not_echo_the_request_id(void)
{
    /* No request id comes back: the reply is delivered with an empty one
     * and matched by the definition's own skill_id/intent_name/lang. */
    const char *frame =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
        "{\"ok\":true,\"definitions\":[{\"method\":\"template\",\"definition\":"
        WEATHER_DEFINITION("fr-FR") "}]},\"context\":{\"lang\":\"fr-fr\",\"session\":"
        "{\"session_id\":\"71048b7f-e7b0-4360-8fb5-a03816f78617\"}}}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-d4", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_DESCRIBE_RESPONSE);
    CHECK_STR_EQ(event.request_id, "");
    CHECK_STR_EQ(event.lang, "fr-fr");
    definition_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &log), 1);
    CHECK_STR_EQ(log.first.skill_id, WEATHER);
    CHECK_STR_EQ(log.first.intent_name, "current.weather");
    CHECK(thalovant_intent_same_language(log.first.lang, "fr-fr"));

    /* The request id may also arrive under the session or in data, as the
     * ask classifier accepts. */
    const char *in_session =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
        "{\"ok\":true,\"definitions\":[]},\"context\":{\"session\":{\"thalovant_request_id\":"
        "\"req-d5\"}}}}";
    CHECK_INT_EQ(classify(in_session, "req-d5", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_DESCRIBE_RESPONSE);
    CHECK_STR_EQ(event.request_id, "req-d5");
    const char *in_data =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.describe.response\",\"data\":"
        "{\"ok\":true,\"definitions\":[],\"correlation_id\":\"req-d6\"}}}";
    CHECK_INT_EQ(classify(in_data, "req-d6", &event), THALOVANT_OK);
    CHECK_STR_EQ(event.request_id, "req-d6");
}

static void test_ignored_frames(void)
{
    thalovant_intent_event event;
    /* Another request's reply. */
    CHECK_INT_EQ(classify(LIST_RESPONSE_EN, "req-2", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
    CHECK_STR_EQ(event.request_id, "");
    /* Ask-loop traffic, non-bus frames, unknown events, and the encrypted
     * envelope itself. */
    const char *speak =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\",\"data\":{\"utterance\":\"x\"},"
        "\"context\":{\"request_id\":\"req-1\"}}}";
    CHECK_INT_EQ(classify(speak, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
    const char *query = "{\"msg_type\":\"query\",\"payload\":{\"type\":\"ovos.intent.list.response\"}}";
    CHECK_INT_EQ(classify(query, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
    const char *unknown = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"mic.level\",\"data\":{}}}";
    CHECK_INT_EQ(classify(unknown, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
    const char *envelope = "{\"ciphertext\":\"00\",\"tag\":\"00\",\"nonce\":\"00\"}";
    CHECK_INT_EQ(classify(envelope, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_IGNORE);
    /* Malformed input is an error, not a guess. */
    CHECK_INT_EQ(classify("{\"msg_type\":\"bus\",\"payload\":", NULL, &event), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(classify("[]", NULL, &event), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(classify("{} x", NULL, &event), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(thalovant_intent_classify(NULL, 0, NULL, &event), THALOVANT_ERR_INVALID);

    /* The walkers refuse the wrong kind of event. */
    row_log rows = { 0 };
    definition_log definitions = { 0 };
    CHECK_INT_EQ(classify(speak, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &rows), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(classify(DESCRIBE_RESPONSE, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &rows), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(classify(LIST_RESPONSE_EN, NULL, &event), THALOVANT_OK);
    CHECK_INT_EQ(thalovant_intent_definitions(&event, log_definition, &definitions),
                 THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, NULL, &rows), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_list_rows(NULL, log_row, &rows), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_samples(NULL, 0, log_sample, &rows), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_intent_samples("[]", 2, log_sample, &rows), THALOVANT_ERR_JSON);
}

static void test_large_manifest_streams(void)
{
    /* 60 rows: well past what THALOVANT_WIRE_MAX_TOKENS could hold, and the
     * walk still delivers every one with a single row on the stack. */
    static char frame[16384];
    size_t pos = 0;
    pos += (size_t)sprintf(frame + pos, "{\"msg_type\":\"bus\",\"payload\":{\"type\":"
                                        "\"ovos.intent.list.response\",\"data\":{\"ok\":true,"
                                        "\"intents\":[");
    for (int i = 0; i < 60; i++) {
        pos += (size_t)sprintf(frame + pos,
                               "%s{\"skill_id\":\"skill-%02d.thalovant\",\"intent_name\":"
                               "\"intent.%02d\",\"lang\":\"en-us\",\"method\":\"%s\","
                               "\"enabled\":true,\"session_id\":\"default\"}",
                               i ? "," : "", i / 4, i, i % 3 ? "template" : "keyword");
    }
    pos += (size_t)sprintf(frame + pos, "]}," ECHOED_CONTEXT("req-big", "en-us") "}}");
    CHECK(pos < sizeof(frame));

    thalovant_json_tok pool[THALOVANT_WIRE_MAX_TOKENS];
    CHECK_INT_EQ(thalovant_json_parse(frame, pos, pool, THALOVANT_WIRE_MAX_TOKENS),
                 THALOVANT_ERR_NOMEM);

    thalovant_intent_event event;
    CHECK_INT_EQ(thalovant_intent_classify(frame, pos, "req-big", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    CHECK_INT_EQ(event.count, 60);
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), 60);
    CHECK_STR_EQ(log.first.skill_id, "skill-00.thalovant");
    CHECK_INT_EQ(log.first.engine, THALOVANT_INTENT_ENGINE_ADAPT);
    CHECK_STR_EQ(log.last.skill_id, "skill-14.thalovant");
    CHECK_STR_EQ(log.last.intent_name, "intent.59");
    CHECK_INT_EQ(log.last.engine, THALOVANT_INTENT_ENGINE_PADATIOUS);
    row_log five = { 0 };
    five.stop_after = 5;
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &five), 5);
}

static void test_field_limits(void)
{
    /* A field over its THALOVANT_*_MAX is refused, never truncated. */
    static char frame[1024];
    char skill[THALOVANT_INTENT_SKILL_ID_MAX + 8];
    memset(skill, 'a', sizeof(skill) - 1);
    skill[sizeof(skill) - 1] = '\0';
    sprintf(frame,
            "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
            "{\"ok\":true,\"intents\":[{\"skill_id\":\"%s\",\"intent_name\":\"n\"}]},"
            "\"context\":{\"request_id\":\"req-1\"}}}",
            skill);
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-1", &event), THALOVANT_OK);
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_NOMEM);

    char sample[THALOVANT_INTENT_SAMPLE_MAX + 8];
    memset(sample, 'b', sizeof(sample) - 1);
    sample[sizeof(sample) - 1] = '\0';
    sprintf(frame, "{\"samples\":[\"ok\",\"%s\"]}", sample);
    sample_log samples = { 0, 0, "" };
    CHECK_INT_EQ(thalovant_intent_samples(frame, strlen(frame), log_sample, &samples),
                 THALOVANT_ERR_NOMEM);
}

static void test_malformed_manifest_is_refused(void)
{
    /* A frame whose rows are not comma-separated: the walk stops at the
     * malformed separator and reports it rather than handing the callback
     * rows read out of unvalidated input. */
    const char *frame =
        "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"ovos.intent.list.response\",\"data\":"
        "{\"ok\":true,\"intents\":[" WEATHER_ROW("en-us") " " SHADOW_ROW("en-us") "]},"
        ECHOED_CONTEXT("req-1", "en-us") "}}";
    thalovant_intent_event event;
    CHECK_INT_EQ(classify(frame, "req-1", &event), THALOVANT_OK);
    CHECK_INT_EQ(event.kind, THALOVANT_INTENT_LIST_RESPONSE);
    row_log log = { 0 };
    CHECK_INT_EQ(thalovant_intent_list_rows(&event, log_row, &log), THALOVANT_ERR_JSON);
    CHECK_INT_EQ(log.rows, 1);
    CHECK_STR_EQ(log.first.skill_id, WEATHER);

    /* Likewise a trailing comma in a definition's samples. */
    const char *definition =
        "{\"skill_id\":\"" WEATHER "\",\"intent_name\":\"current.weather\",\"lang\":"
        "\"en-us\",\"samples\":[\"what is the weather\",]}";
    sample_log samples = { 0, 0, "" };
    CHECK_INT_EQ(thalovant_intent_samples(definition, strlen(definition), log_sample, &samples),
                 THALOVANT_ERR_JSON);
    CHECK_INT_EQ(samples.count, 1);
}

void tlv_test_intents(void)
{
    test_build_list_query();
    test_build_describe_query();
    test_list_response_rows();
    test_list_response_shapes();
    test_a_refused_listing_is_not_an_empty_hub();
    test_duplicate_delivery_takes_the_first();
    test_language_tags_fold();
    test_policy_denied();
    test_the_allowed_list_holds_only_message_types();
    test_include_definitions_fast_path();
    test_describe_response();
    test_describe_shapes();
    test_hub_that_does_not_echo_the_request_id();
    test_ignored_frames();
    test_large_manifest_streams();
    test_field_limits();
    test_malformed_manifest_is_refused();
}
