/*
 * Compile-time configuration for the Thalovant embedded C client library.
 *
 * Every limit can be overridden from the build system, e.g.
 *   -DTHALOVANT_TOPIC_MAX=320
 * The defaults are sized for typical HiveMind identities issued by the
 * Thalovant API while keeping stack usage modest on MCU-class targets.
 */
#ifndef THALOVANT_CONFIG_H
#define THALOVANT_CONFIG_H

/* Identity string fields (bytes, including the NUL terminator). */
#ifndef THALOVANT_ACCESS_KEY_MAX
#define THALOVANT_ACCESS_KEY_MAX 128
#endif
#ifndef THALOVANT_PASSWORD_MAX
#define THALOVANT_PASSWORD_MAX 128
#endif
#ifndef THALOVANT_CRYPTO_KEY_MAX
#define THALOVANT_CRYPTO_KEY_MAX 64
#endif
#ifndef THALOVANT_SITE_ID_MAX
#define THALOVANT_SITE_ID_MAX 64
#endif
#ifndef THALOVANT_MASTER_MAX
#define THALOVANT_MASTER_MAX 128
#endif
#ifndef THALOVANT_PATH_MAX
#define THALOVANT_PATH_MAX 64
#endif
#ifndef THALOVANT_PUBLIC_KEY_MAX
#define THALOVANT_PUBLIC_KEY_MAX 256
#endif

/* MQTT credential fields. */
#ifndef THALOVANT_MQTT_ENDPOINT_MAX
#define THALOVANT_MQTT_ENDPOINT_MAX 128
#endif
#ifndef THALOVANT_MQTT_USERNAME_MAX
#define THALOVANT_MQTT_USERNAME_MAX 96
#endif
#ifndef THALOVANT_MQTT_PASSWORD_MAX
#define THALOVANT_MQTT_PASSWORD_MAX 128
#endif
#ifndef THALOVANT_MQTT_TOPIC_PREFIX_MAX
#define THALOVANT_MQTT_TOPIC_PREFIX_MAX 160
#endif
#ifndef THALOVANT_MQTT_HUB_ID_MAX
#define THALOVANT_MQTT_HUB_ID_MAX 64
#endif

/* Derived MQTT topics. */
#ifndef THALOVANT_TOPIC_MAX
#define THALOVANT_TOPIC_MAX 256
#endif

/* JSON tokenizer defaults. */
#ifndef THALOVANT_JSON_MAX_DEPTH
#define THALOVANT_JSON_MAX_DEPTH 24
#endif
#ifndef THALOVANT_IDENTITY_MAX_TOKENS
#define THALOVANT_IDENTITY_MAX_TOKENS 192
#endif
#ifndef THALOVANT_WIRE_MAX_TOKENS
#define THALOVANT_WIRE_MAX_TOKENS 256
#endif

/* Wire frame scalar fields. */
#ifndef THALOVANT_MSG_TYPE_MAX
#define THALOVANT_MSG_TYPE_MAX 32
#endif
#ifndef THALOVANT_WIRE_NODE_MAX
#define THALOVANT_WIRE_NODE_MAX 96
#endif

/* Extracted utterance/speak text in the ask helpers. */
#ifndef THALOVANT_ASK_TEXT_MAX
#define THALOVANT_ASK_TEXT_MAX 512
#endif
#ifndef THALOVANT_REQUEST_ID_MAX
#define THALOVANT_REQUEST_ID_MAX 96
#endif

/* Intent inventory (thalovant_intents): manifest rows and definitions. */
#ifndef THALOVANT_LANG_MAX
#define THALOVANT_LANG_MAX 16
#endif
#ifndef THALOVANT_EVENT_NAME_MAX
#define THALOVANT_EVENT_NAME_MAX 64
#endif
#ifndef THALOVANT_INTENT_SKILL_ID_MAX
#define THALOVANT_INTENT_SKILL_ID_MAX 64
#endif
#ifndef THALOVANT_INTENT_NAME_MAX
#define THALOVANT_INTENT_NAME_MAX 64
#endif
#ifndef THALOVANT_INTENT_METHOD_MAX
#define THALOVANT_INTENT_METHOD_MAX 16
#endif
#ifndef THALOVANT_INTENT_SESSION_ID_MAX
#define THALOVANT_INTENT_SESSION_ID_MAX 64
#endif
#ifndef THALOVANT_INTENT_SAMPLE_MAX
#define THALOVANT_INTENT_SAMPLE_MAX 256
#endif
#ifndef THALOVANT_INTENT_ERROR_MAX
#define THALOVANT_INTENT_ERROR_MAX 128
#endif
/* hive.policy.denied fields. */
#ifndef THALOVANT_POLICY_CODE_MAX
#define THALOVANT_POLICY_CODE_MAX 32
#endif
#ifndef THALOVANT_POLICY_REASON_MAX
#define THALOVANT_POLICY_REASON_MAX 160
#endif

#endif /* THALOVANT_CONFIG_H */
