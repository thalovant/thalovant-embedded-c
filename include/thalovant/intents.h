/*
 * Intent inventory: what the hub can be asked, read over the satellite's own
 * session with no control-plane credential (platform contract "Hub intent
 * inventory"; the Python SDK's thalovant.intents is the reference).
 *
 * The hub runtime keeps an intent manifest (OVOS-INTENT-4 section 10): every
 * intent a skill registered, per language, and on request the registration
 * itself, which for a template intent carries the sentences from the skill's
 * locale files, slots and all -- "what is the weather in {location}".
 *
 * Two bus queries, correlated by context.request_id like ask():
 *
 *   ovos.intent.list {"lang"}  ->  ovos.intent.list.response
 *       {"ok", "intents": [{skill_id, intent_name, lang, method, enabled,
 *                           session_id}]}
 *       `method` is "template" (sample sentences, engine padatious) or
 *       "keyword" (keyword sets, engine adapt). A runtime asked with
 *       {"include_definitions": true} may attach each row's "definition";
 *       use it when present and describe row by row when absent.
 *
 *   ovos.intent.describe {"skill_id", "intent_name", "lang"}
 *       ->  ovos.intent.describe.response
 *       {"ok", "definitions": [{method, definition}]} or {"ok": false, "error"}
 *       A template definition carries "samples", the sentences with {slot}
 *       placeholders, plus its own skill_id/intent_name/lang.
 *
 * Correlation: the reply echoes context.request_id and the lang sent in the
 * context. A reply may be delivered more than once: take the first one for a
 * request id and ignore repeats. Describes may be sent together, one request
 * id each; match the replies by request id, or by the definition's own
 * skill_id/intent_name/lang when a hub does not echo the id (the classifier
 * delivers such a reply with an empty request_id rather than ignoring it --
 * the one difference from thalovant_ask_classify's required-correlation
 * rule, and what the contract asks for). A describe that never arrives
 * leaves that intent without sentences; it does not fail the inventory.
 *
 * Refusal: a connection not allowed to publish a type receives
 * hive.policy.denied {denied_type, code: "acl_disallowed_type", reason,
 * data: {msg_type, allowed}} and no reply. THALOVANT_INTENT_POLICY_DENIED
 * surfaces it at once, naming the type, so the caller need not wait out a
 * timeout; the row walkers return THALOVANT_ERR_POLICY_DENIED for it.
 * A connection needs ovos.intent.list in its allow-list to read the
 * manifest at all; ovos.intent.describe only when it goes on to ask for
 * the sentences behind a registration.
 *
 * A negative answer is not the same on both queries. A listing answering
 * {"ok": false, "error": ...} has told us nothing, so
 * thalovant_intent_list_rows returns THALOVANT_ERR_HUB_REFUSED rather than
 * the zero rows that would show a person a hub able to do nothing; the
 * hub's own words are in the event's `error`. A describe answering
 * ok: false is a real answer -- the hub does not know that registration --
 * and thalovant_intent_definitions delivers nothing for it, exactly as an
 * intent with no sentences.
 *
 * Language tags compare case-insensitively with "_" and "-" folded
 * (thalovant_intent_same_language): the runtime answers "fr-fr" as "fr-FR".
 *
 * Memory: the list reply is unbounded (a hub registers hundreds of rows), so
 * nothing here tokenizes the whole frame. The classifier finds the reply's
 * fields with shallow scans and keeps raw slices into the caller's frame
 * buffer; the walkers then deliver one row, definition, or sample at a time
 * through a callback into caller-owned structs. The frame buffer must stay
 * alive while the event and anything walked from it are in use.
 */
#ifndef THALOVANT_INTENTS_H
#define THALOVANT_INTENTS_H

#include <stdbool.h>
#include <stddef.h>

#include "thalovant/config.h"
#include "thalovant/error.h"

/* ---------------------------------------------------------- event names */

#define THALOVANT_EVENT_INTENT_LIST "ovos.intent.list"
#define THALOVANT_EVENT_INTENT_LIST_RESPONSE "ovos.intent.list.response"
#define THALOVANT_EVENT_INTENT_DESCRIBE "ovos.intent.describe"
#define THALOVANT_EVENT_INTENT_DESCRIBE_RESPONSE "ovos.intent.describe.response"
#define THALOVANT_EVENT_POLICY_DENIED "hive.policy.denied"
/*
 * The engines' own manifests: names only ("<skill_id>:<intent_name>"), no
 * language -- the names-only fallback of the desktop SDKs for a connection
 * refused ovos.intent.list. This library defines the names and leaves the
 * query to the integrator (a plain bus frame; the reply is {"intents": [..]}).
 */
#define THALOVANT_EVENT_ADAPT_MANIFEST_GET "intent.service.adapt.manifest.get"
#define THALOVANT_EVENT_ADAPT_MANIFEST "intent.service.adapt.manifest"
#define THALOVANT_EVENT_PADATIOUS_MANIFEST_GET "intent.service.padatious.manifest.get"
#define THALOVANT_EVENT_PADATIOUS_MANIFEST "intent.service.padatious.manifest"
/* The `code` a hive.policy.denied carries for a type outside the allow-list. */
#define THALOVANT_POLICY_CODE_ACL_DISALLOWED_TYPE "acl_disallowed_type"

/* ------------------------------------------------------------- queries */

typedef struct {
    const char *lang;          /* NULL -> "en-us" */
    const char *session_id;    /* required: this connection's session */
    const char *site_id;       /* optional (identity.site_id); NULL to omit */
    const char *request_id;    /* required correlation id, unique per query */
    bool include_definitions;  /* ask the runtime to attach each row's definition */
} thalovant_intent_list_request;

/*
 * The bus payload only:
 *   {"type":"ovos.intent.list","data":{"lang":"<lang>"[,"include_definitions":true]},
 *    "context":{"request_id":"<R>","thalovant_request_id":"<R>","lang":"<lang>",
 *      "session":{"session_id":"<S>","site_id":"<site>","lang":"<lang>","request_id":"<R>"}}}
 * Returns the length written.
 */
int thalovant_intent_list_build_payload(const thalovant_intent_list_request *request, char *out,
                                        size_t cap);

/* The full HiveMessage frame, ready to seal and send. */
int thalovant_intent_list_build_frame(const thalovant_intent_list_request *request, char *out,
                                      size_t cap);

typedef struct {
    const char *skill_id;    /* required */
    const char *intent_name; /* required */
    const char *lang;        /* NULL -> "en-us" */
    const char *session_id;  /* required */
    const char *site_id;     /* optional; NULL to omit */
    const char *request_id;  /* required correlation id, unique per query */
} thalovant_intent_describe_request;

/*
 * {"type":"ovos.intent.describe","data":{"skill_id":..,"intent_name":..,"lang":..},
 *  "context":{...as above...}}
 */
int thalovant_intent_describe_build_payload(const thalovant_intent_describe_request *request,
                                            char *out, size_t cap);

int thalovant_intent_describe_build_frame(const thalovant_intent_describe_request *request,
                                          char *out, size_t cap);

/* ------------------------------------------------------------- replies */

typedef enum {
    THALOVANT_INTENT_IGNORE = 0,        /* unrelated frame or correlation mismatch */
    THALOVANT_INTENT_LIST_RESPONSE,     /* "ovos.intent.list.response" */
    THALOVANT_INTENT_DESCRIBE_RESPONSE, /* "ovos.intent.describe.response" */
    THALOVANT_INTENT_POLICY_DENIED,     /* "hive.policy.denied" */
} thalovant_intent_kind;

typedef enum {
    THALOVANT_INTENT_ENGINE_UNKNOWN = 0,
    THALOVANT_INTENT_ENGINE_PADATIOUS, /* method "template": sample sentences */
    THALOVANT_INTENT_ENGINE_ADAPT,     /* method "keyword": keyword sets */
} thalovant_intent_engine;

typedef struct {
    thalovant_intent_kind kind;
    /* The reply's correlation id; "" when the hub echoed none. */
    char request_id[THALOVANT_REQUEST_ID_MAX];
    /* data.lang ?? context.lang ?? context.session.lang; "" when absent. */
    char lang[THALOVANT_LANG_MAX];

    /* LIST_RESPONSE / DESCRIBE_RESPONSE: data.ok (true unless it says
     * false) and data.error ("" when absent). */
    bool ok;
    char error[THALOVANT_INTENT_ERROR_MAX];
    /* Raw JSON slice of data.intents (list) or data.definitions (describe)
     * inside the frame buffer, and its element count; NULL/0 when absent.
     * Walk it with thalovant_intent_list_rows / thalovant_intent_definitions. */
    const char *items_json;
    size_t items_len;
    int count;

    /* POLICY_DENIED: the type this connection may not publish, the code
     * (THALOVANT_POLICY_CODE_ACL_DISALLOWED_TYPE), the reason, and the raw
     * JSON slice of data.data.allowed -- the types it may -- with the number
     * of *string* entries in it, which is what thalovant_intent_allowed_types
     * delivers. A list holding nothing usable, or one too malformed to walk,
     * leaves allowed_json NULL and allowed_count 0; the denial itself still
     * names denied_type, code and reason. */
    char denied_type[THALOVANT_EVENT_NAME_MAX];
    char code[THALOVANT_POLICY_CODE_MAX];
    char reason[THALOVANT_POLICY_REASON_MAX];
    const char *allowed_json;
    size_t allowed_len;
    int allowed_count;
} thalovant_intent_event;

/*
 * Classify a decrypted plaintext HiveMessage frame against `request_id`
 * (NULL accepts any correlation). A bare bus payload {type,data,context}
 * -- what thalovant_wire_decode_binary yields -- is accepted as well as the
 * full frame. Non-bus frames and unrelated bus events come back as
 * THALOVANT_INTENT_IGNORE, as does a reply carrying a different request
 * id; a reply carrying none is delivered with an empty request_id (see the
 * header comment). Returns THALOVANT_ERR_JSON for a malformed frame and
 * THALOVANT_ERR_NOMEM when a field exceeds its THALOVANT_*_MAX.
 */
int thalovant_intent_classify(const char *frame_json, size_t len, const char *request_id,
                              thalovant_intent_event *out);

/* One row of the hub's intent manifest (ovos.intent.list.response). */
typedef struct {
    char skill_id[THALOVANT_INTENT_SKILL_ID_MAX];
    char intent_name[THALOVANT_INTENT_NAME_MAX];
    char lang[THALOVANT_LANG_MAX];
    char method[THALOVANT_INTENT_METHOD_MAX]; /* "template" / "keyword" as sent */
    thalovant_intent_engine engine;           /* method mapped; UNKNOWN otherwise */
    bool enabled;                             /* true unless the row says false */
    char session_id[THALOVANT_INTENT_SESSION_ID_MAX]; /* "default" when absent */
    /* The row's definition when the runtime honoured include_definitions --
     * a raw JSON object slice for thalovant_intent_samples -- else NULL. */
    const char *definition_json;
    size_t definition_len;
} thalovant_intent_registration;

/* A registration as the skill made it (ovos.intent.describe.response). */
typedef struct {
    char skill_id[THALOVANT_INTENT_SKILL_ID_MAX];
    char intent_name[THALOVANT_INTENT_NAME_MAX];
    char lang[THALOVANT_LANG_MAX];
    char method[THALOVANT_INTENT_METHOD_MAX]; /* item.method ?? definition.method */
    thalovant_intent_engine engine;
    /* The definition object, raw, for thalovant_intent_samples. */
    const char *definition_json;
    size_t definition_len;
    /* Elements of definition.samples; 0 for a keyword intent. */
    int sample_count;
} thalovant_intent_definition;

/* One sentence of a template definition, as the skill's locale file wrote it. */
typedef struct {
    int index;     /* position among the non-empty samples, from 0 */
    bool has_slot; /* carries a {slot} placeholder; whole sentences show best */
    char text[THALOVANT_INTENT_SAMPLE_MAX];
} thalovant_intent_sample;

/* Callbacks return true to continue the walk and false to stop it. */
typedef bool (*thalovant_intent_registration_fn)(const thalovant_intent_registration *row,
                                                 void *user);
typedef bool (*thalovant_intent_definition_fn)(const thalovant_intent_definition *definition,
                                               void *user);
typedef bool (*thalovant_intent_sample_fn)(const thalovant_intent_sample *sample, void *user);

/* One message type from a denial's allow-list. */
typedef struct {
    int index; /* position among the string entries, from 0 */
    char type[THALOVANT_EVENT_NAME_MAX];
} thalovant_intent_allowed_type;

typedef bool (*thalovant_intent_allowed_fn)(const thalovant_intent_allowed_type *allowed,
                                            void *user);

/*
 * Deliver each row of a THALOVANT_INTENT_LIST_RESPONSE event. Rows without
 * a skill_id or intent_name are skipped, like the reference SDK. Returns the
 * number delivered, THALOVANT_ERR_HUB_REFUSED for a reply that says
 * ok:false (a refused listing is not an empty hub; the event's `error`
 * carries the hub's text), THALOVANT_ERR_POLICY_DENIED for a POLICY_DENIED
 * event, THALOVANT_ERR_INVALID for any other kind, and THALOVANT_ERR_NOMEM
 * when a field exceeds its limit.
 */
int thalovant_intent_list_rows(const thalovant_intent_event *event,
                               thalovant_intent_registration_fn fn, void *user);

/*
 * Deliver each definition of a THALOVANT_INTENT_DESCRIBE_RESPONSE event,
 * keyword ones first as the hub orders them. Items whose definition names
 * no skill_id/intent_name are skipped. Returns the number delivered -- 0
 * for a reply that says ok:false, which is the hub answering that it does
 * not know the registration, and so leaves the intent without sentences
 * rather than failing the inventory -- and otherwise the same errors as
 * thalovant_intent_list_rows.
 */
int thalovant_intent_definitions(const thalovant_intent_event *event,
                                 thalovant_intent_definition_fn fn, void *user);

/*
 * Deliver each non-empty, whitespace-trimmed sentence of a definition's
 * "samples" (from a registration row's or a definition's definition_json).
 * Returns the number delivered; 0 when the definition has no samples.
 */
int thalovant_intent_samples(const char *definition_json, size_t len,
                             thalovant_intent_sample_fn fn, void *user);

/*
 * Deliver each message type a THALOVANT_INTENT_POLICY_DENIED event says the
 * connection may publish. Only the list's string entries are delivered: a
 * number or a null there is not a message type, and naming one would send
 * an operator reading which types to allow after "3" or "null". Returns the
 * number delivered (0 when the denial named none), THALOVANT_ERR_INVALID
 * for any other kind of event, THALOVANT_ERR_NOMEM for a type longer than
 * THALOVANT_EVENT_NAME_MAX, and THALOVANT_ERR_JSON for a malformed list.
 */
int thalovant_intent_allowed_types(const thalovant_intent_event *event,
                                   thalovant_intent_allowed_fn fn, void *user);

/* "fr-fr", "fr_FR" and " FR-fr " name the same language. NULL reads as "". */
bool thalovant_intent_same_language(const char *a, const char *b);

#endif /* THALOVANT_INTENTS_H */
