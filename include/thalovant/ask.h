/*
 * Ask-loop helpers: build the recognizer_loop:utterance frame and classify
 * the frames that come back, so integrators can run the Node SDK's ask()
 * state machine on their own event loop.
 *
 * Outgoing frame (byte-comparable with the Node SDK's emitBus output):
 *
 *   {"msg_type":"bus","payload":{"type":"recognizer_loop:utterance",
 *    "data":{"utterances":["<text>"],"lang":"<lang>"},
 *    "context":{"request_id":"<R>","thalovant_request_id":"<R>",
 *      "session":{"session_id":"<S>","site_id":"<site>","lang":"<lang>",
 *                 "request_id":"<R>"}}}, ...explicit nulls...}
 *
 * Reference ask() semantics (client.ts) for the classifier results:
 *  - SPEAK: collect the text (dedupe consecutive duplicates).
 *  - HANDLED ("ovos.utterance.handled"): the hub finished routing; keep
 *    waiting briefly for a speak reply if none arrived yet.
 *  - INTENT_FAILURE ("complete_intent_failure" legacy Mycroft name, or
 *    "ovos.intent.unmatched" current OVOS name): recorded as a failure
 *    event but does not terminate the wait by itself. Later speech or a
 *    successful handled event supersedes this soft miss; a fallback may
 *    answer after the primary intent engine found no match.
 *  - POLICY_DENIED ("hive.policy.denied") and QUERY_TIMEOUT
 *    ("hive.query.timeout"): terminal failures.
 * Events whose request id does not match — or that carry no request id at
 * all — are IGNOREd, matching the Node SDK's required-correlation rule.
 */
#ifndef THALOVANT_ASK_H
#define THALOVANT_ASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thalovant/config.h"
#include "thalovant/error.h"

typedef struct {
    const char *text;       /* required, non-empty utterance */
    const char *lang;       /* NULL -> "en-us" */
    const char *session_id; /* required */
    const char *site_id;    /* optional (identity.site_id); NULL to omit */
    const char *request_id; /* required correlation id */
} thalovant_ask_request;

/* The bus payload only: {"type":...,"data":...,"context":...}. */
int thalovant_ask_build_payload(const thalovant_ask_request *request, char *out, size_t cap);

/* The full HiveMessage frame, ready to seal and send. */
int thalovant_ask_build_frame(const thalovant_ask_request *request, char *out, size_t cap);

typedef enum {
    THALOVANT_ASK_IGNORE = 0,     /* unrelated frame or correlation mismatch */
    THALOVANT_ASK_SPEAK,          /* "speak" / "ovos.utterance.speak" */
    THALOVANT_ASK_HANDLED,        /* "ovos.utterance.handled" */
    THALOVANT_ASK_INTENT_FAILURE, /* "complete_intent_failure" / "ovos.intent.unmatched" */
    THALOVANT_ASK_POLICY_DENIED,  /* "hive.policy.denied" */
    THALOVANT_ASK_QUERY_TIMEOUT,  /* "hive.query.timeout" */
    THALOVANT_ASK_AUDIO,          /* "mycroft.audio.queue": collect without settling */
} thalovant_ask_kind;

typedef struct {
    thalovant_ask_kind kind;
    /* True for the three failure kinds (FAILURE_EVENTS in the Node SDK). */
    bool is_failure;
    /* data.utterance ?? data.text ?? data.utterances[0]; "" when absent. */
    char text[THALOVANT_ASK_TEXT_MAX];
    /* The event's correlation id; "" when the event carried none. */
    char request_id[THALOVANT_REQUEST_ID_MAX];
} thalovant_ask_event;

/*
 * Classify a decrypted plaintext HiveMessage frame against `request_id`
 * (NULL accepts any correlation). Non-bus frames and unknown bus event
 * types come back as THALOVANT_ASK_IGNORE.
 */
int thalovant_ask_classify(const char *frame_json, size_t len, const char *request_id,
                           thalovant_ask_event *out);

/*
 * Normalize speak text the way ask() does before collecting fragments:
 * trim and collapse whitespace runs to single spaces. Returns the new
 * length.
 */
int thalovant_ask_normalize_text(char *text);

/* Optional request hints. JSON values must be a pipeline array and location object.
 * Each decoded pipeline stage must fit THALOVANT_ASK_TEXT_MAX including its NUL
 * terminator. Hint JSON must fit THALOVANT_WIRE_MAX_TOKENS. The complete frame is
 * bounded by the caller's output buffer, with no intermediate payload-size cap.
 * Existing request/frame builders retain their exact wire shape. */
typedef struct {
    const char *stt_lang;
    const char *pipeline_json;
    const char *location_json;
} thalovant_ask_hints;
int thalovant_ask_build_payload_with_hints(const thalovant_ask_request *request,
    const thalovant_ask_hints *hints, char *out, size_t cap);
int thalovant_ask_build_frame_with_hints(const thalovant_ask_request *request,
    const thalovant_ask_hints *hints, char *out, size_t cap);

#define THALOVANT_AUDIO_CLIP_MAX (4u * 1024u * 1024u)
#define THALOVANT_REPLY_MEDIA_MAX (16u * 1024u * 1024u)
/* Decode only embedded hexadecimal bytes. Scratch holds the JSON-unescaped
 * encoded string; output holds the decoded clip. No paths or URLs are fetched.
 * Returns decoded byte count, ERR_MISSING for non-audio/missing/correlation miss,
 * ERR_INVALID for malformed hex, ERR_NOMEM for clip or caller-buffer limits. */
int thalovant_ask_audio_decode(const char *frame, size_t len, const char *request_id,
    char *scratch, size_t scratch_cap, uint8_t *out, size_t out_cap);
/* Returns the first nonempty data.lang/context.lang/context.session.lang. */
int thalovant_ask_event_language(const char *frame, size_t len, const char *request_id,
    char *out, size_t cap);
/* Copy a nonempty string stamp from a recognized, correlated reply event.
 * Returns decoded length, ERR_MISSING for absent/non-string stamps or unrelated
 * frames, ERR_INVALID for embedded NUL, and ERR_NOMEM for insufficient space.
 * Callers retain distinct IDs in first-seen order using their own storage. */
int thalovant_ask_event_pipeline_id(const char *frame, size_t len, const char *request_id,
    char *out, size_t cap);
int thalovant_ask_event_skill_id(const char *frame, size_t len, const char *request_id,
    char *out, size_t cap);
/* Advisory reply claim: false for failed/unhandled or fallback-only replies;
 * true for successful replies with a non-fallback stage or no nonempty stamps.
 * NULL/empty entries are ignored. A NULL array with nonzero count fails closed.
 * IDs and array storage remain caller-owned; this is not origin verification. */
bool thalovant_reply_claimed(bool handled, bool has_failure,
    const char *const *pipeline_ids, size_t pipeline_count);

/*
 * What the hub said when it refused, and whose refusal it is.
 *
 * The hub sends hive.policy.denied the instant it refuses, built with source
 * and destination context only, so it carries no request id and names the type
 * it refused instead. Three different things arrive under that one name and
 * each needs something different said about it: an allow-list refusal
 * (acl_disallowed_type, with the allowed list), a spent allowance
 * (intent_quota_exceeded, with the numbers), and a hub whose agent bus is down
 * (backend_unavailable), which nothing the caller does will fix.
 *
 * The policy's own detail rides nested under data.data (hivemind-core
 * _send_policy_denied: "data": verdict.data). Shared with every other SDK
 * through contracts/conformance/refusal-vectors.json.
 */
#define THALOVANT_POLICY_QUOTA_EXCEEDED "intent_quota_exceeded"
#define THALOVANT_POLICY_BACKEND_UNAVAILABLE "backend_unavailable"

/*
 * How long a fire-and-forget utterance counts as possibly still being refused.
 * Denials come back as fast as the hub admits a message, so this is generous
 * on purpose: a wrong "in flight" only costs an ask the deadline it always
 * had, where a wrong "not in flight" ends a question the hub never refused.
 */
#define THALOVANT_UNTRACKED_UTTERANCE_GRACE_SECONDS 10

/*
 * The largest count the wire can carry, being the largest whole number every
 * JSON decoder holds exactly. Above it a decoder backed by a double can no
 * longer tell one whole number from the next, so two SDKs would report
 * different allowances for the same denial. Anything larger, negative or
 * fractional reads as 0.
 */
#define THALOVANT_POLICY_COUNT_MAX INT64_C(9007199254740991)

typedef struct {
    char denied_type[THALOVANT_ASK_TEXT_MAX];
    char code[THALOVANT_POLICY_CODE_MAX];
    char reason[THALOVANT_POLICY_REASON_MAX];
    /* True only for THALOVANT_POLICY_QUOTA_EXCEEDED; the rest are then zero. */
    bool has_quota;
    char quota_period[THALOVANT_POLICY_CODE_MAX];
    /* Whole, non-negative, and no larger than THALOVANT_POLICY_COUNT_MAX: a
     * negative limit, usage or reset time is not something a policy can mean,
     * and would have an app say "-1 of -5". int64_t rather than long, because
     * the ceiling is the contract's and does not shrink on a 32-bit target. */
    int64_t quota_limit;
    int64_t quota_used;
    int64_t quota_reset_after;
} thalovant_refusal;

/*
 * Read a hive.policy.denied frame. Returns 0, ERR_MISSING for a frame that is
 * not a denial (or fails correlation), ERR_INVALID for bad arguments, and
 * ERR_NOMEM when a field does not fit its bounded buffer.
 */
int thalovant_ask_refusal(const char *frame, size_t len, const char *request_id,
    thalovant_refusal *out);

/*
 * Copy the nth message type this connection may publish, as the hub listed
 * them: non-empty strings only, trimmed. Returns the length, or ERR_MISSING
 * past the end. Storage stays the caller's, as everywhere else here.
 */
int thalovant_ask_refusal_allowed(const char *frame, size_t len, size_t index,
    char *out, size_t cap);

/*
 * Whether a denial is this ask's to fail with. A denial carrying a request id
 * is judged by it, like any reply; without one it is this ask's only when it
 * names the type this ask sent and this ask is the only utterance out. The
 * counts are the caller's to keep, as correlation ids are: a second ask, a
 * query, or a fire-and-forget utterance still inside the grace window means
 * either could be the one refused, and a wrong guess ends a question the hub
 * never refused.
 */
bool thalovant_refusal_belongs_to_ask(const char *request_id, const char *own_request_id,
    const char *denied_type, size_t asks_in_flight, size_t queries_in_flight,
    size_t sends_in_flight);

/* Call before retaining a clip. Zero-initialize once per reply. Drop does not
 * settle a reply. Integrators deduplicate their own transport deliveries. */
typedef struct { size_t encoded_chars; size_t dropped; } thalovant_audio_budget;
bool thalovant_audio_budget_accept(thalovant_audio_budget *budget, size_t encoded_chars);

#endif /* THALOVANT_ASK_H */
