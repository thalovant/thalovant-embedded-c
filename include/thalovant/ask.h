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
 *  - INTENT_FAILURE ("complete_intent_failure"): recorded as a failure
 *    event but does not terminate the wait by itself.
 *  - POLICY_DENIED ("hive.policy.denied") and QUERY_TIMEOUT
 *    ("hive.query.timeout"): terminal failures.
 * Events whose request id does not match — or that carry no request id at
 * all — are IGNOREd, matching the Node SDK's required-correlation rule.
 */
#ifndef THALOVANT_ASK_H
#define THALOVANT_ASK_H

#include <stdbool.h>
#include <stddef.h>

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
    THALOVANT_ASK_INTENT_FAILURE, /* "complete_intent_failure" */
    THALOVANT_ASK_POLICY_DENIED,  /* "hive.policy.denied" */
    THALOVANT_ASK_QUERY_TIMEOUT,  /* "hive.query.timeout" */
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

#endif /* THALOVANT_ASK_H */
