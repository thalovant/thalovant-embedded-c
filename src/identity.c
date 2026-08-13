#include "thalovant/identity.h"

#include <string.h>

#include "thalovant/json.h"

#define ALIASES(...) ((const char *const[]){ __VA_ARGS__ })

/*
 * Fetch an aliased string field into `out`. Absent (or null) fields leave
 * `out` untouched (callers pre-zero the struct). Returns THALOVANT_OK,
 * THALOVANT_ERR_MISSING, or a negative coercion error.
 */
static int get_string(const char *js, const thalovant_json_tok *toks, int count, int obj,
                      const char *const *aliases, size_t alias_count, char *out, size_t cap)
{
    int value = thalovant_json_object_get_alias(js, toks, count, obj, aliases, alias_count);
    if (value < 0) {
        return THALOVANT_ERR_MISSING;
    }
    int len = thalovant_json_as_string(js, &toks[value], out, cap);
    if (len < 0) {
        return len;
    }
    return THALOVANT_OK;
}

static void strip_trailing_slashes(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == '/') {
        s[--len] = '\0';
    }
}

/* Normalize a path the way the reference SDKs do: "/x/" -> "/x", "" -> "". */
static int normalize_path(char *path, size_t cap)
{
    size_t len = strlen(path);
    size_t start = 0;
    while (start < len && path[start] == '/') {
        start++;
    }
    size_t end = len;
    while (end > start && path[end - 1] == '/') {
        end--;
    }
    size_t n = end - start;
    if (n == 0) {
        path[0] = '\0';
        return THALOVANT_OK;
    }
    if (n + 2 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memmove(path + 1, path + start, n);
    path[0] = '/';
    path[n + 1] = '\0';
    return THALOVANT_OK;
}

static int parse_mqtt(const char *js, const thalovant_json_tok *toks, int count, int obj,
                      thalovant_mqtt_credentials *mqtt)
{
    int rc;
    rc = get_string(js, toks, count, obj, ALIASES("endpoint", "broker_url", "brokerUrl"), 3,
                    mqtt->endpoint, sizeof(mqtt->endpoint));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("username", "broker_username", "brokerUsername"),
                    3, mqtt->username, sizeof(mqtt->username));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("password", "broker_password", "brokerPassword"),
                    3, mqtt->password, sizeof(mqtt->password));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    if (mqtt->endpoint[0] == '\0' || mqtt->username[0] == '\0' || mqtt->password[0] == '\0') {
        /* Credentials are unusable without all three: treat as absent. */
        return THALOVANT_OK;
    }
    rc = get_string(js, toks, count, obj, ALIASES("topic_prefix", "topicPrefix"), 2,
                    mqtt->topic_prefix, sizeof(mqtt->topic_prefix));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("hub_id", "hubId"), 2, mqtt->hub_id,
                    sizeof(mqtt->hub_id));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("c2s_topic", "c2sTopic"), 2, mqtt->c2s_topic,
                    sizeof(mqtt->c2s_topic));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("s2c_topic", "s2cTopic"), 2, mqtt->s2c_topic,
                    sizeof(mqtt->s2c_topic));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(js, toks, count, obj, ALIASES("status_topic", "statusTopic"), 2,
                    mqtt->status_topic, sizeof(mqtt->status_topic));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;

    mqtt->hash_topics = false;
    int value = thalovant_json_object_get_alias(js, toks, count, obj,
                                                ALIASES("hash_topics", "hashTopics"), 2);
    if (value >= 0) {
        mqtt->hash_topics = thalovant_json_as_bool(js, &toks[value], false);
    }

    mqtt->qos = 1;
    value = thalovant_json_object_get_alias(js, toks, count, obj, ALIASES("qos"), 1);
    if (value >= 0) {
        long qos;
        if (thalovant_json_as_int(js, &toks[value], &qos) == THALOVANT_OK &&
            (qos == 0 || qos == 1)) {
            mqtt->qos = (int)qos;
        }
    }

    bool tls_default = strncmp(mqtt->endpoint, "mqtts://", 8) == 0;
    mqtt->tls = tls_default;
    value = thalovant_json_object_get_alias(js, toks, count, obj, ALIASES("tls"), 1);
    if (value >= 0) {
        mqtt->tls = thalovant_json_as_bool(js, &toks[value], tls_default);
    }

    mqtt->present = true;
    return THALOVANT_OK;
}

int thalovant_identity_parse(const char *json, size_t len, thalovant_identity *out)
{
    if (json == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    thalovant_json_tok toks[THALOVANT_IDENTITY_MAX_TOKENS];
    int count = thalovant_json_parse(json, len, toks, THALOVANT_IDENTITY_MAX_TOKENS);
    if (count < 0) {
        return count;
    }
    if (toks[0].type != THALOVANT_JSON_OBJECT) {
        return THALOVANT_ERR_JSON;
    }

    int rc;
    rc = get_string(json, toks, count, 0, ALIASES("access_key", "accessKey", "api_key", "key"), 4,
                    out->access_key, sizeof(out->access_key));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(json, toks, count, 0, ALIASES("password"), 1, out->password,
                    sizeof(out->password));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(json, toks, count, 0, ALIASES("crypto_key", "cryptoKey"), 2, out->crypto_key,
                    sizeof(out->crypto_key));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(json, toks, count, 0, ALIASES("site_id", "siteId", "site"), 3, out->site_id,
                    sizeof(out->site_id));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = get_string(json, toks, count, 0,
                    ALIASES("default_master", "defaultMaster", "hub_http_host", "host", "master"),
                    5, out->default_master, sizeof(out->default_master));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    strip_trailing_slashes(out->default_master);
    rc = get_string(json, toks, count, 0,
                    ALIASES("default_path", "defaultPath", "hub_http_path", "path", "uri_path"), 5,
                    out->default_path, sizeof(out->default_path));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;
    rc = normalize_path(out->default_path, sizeof(out->default_path));
    if (rc != THALOVANT_OK) return rc;
    rc = get_string(json, toks, count, 0, ALIASES("public_key", "publicKey"), 2, out->public_key,
                    sizeof(out->public_key));
    if (rc < 0 && rc != THALOVANT_ERR_MISSING) return rc;

    out->default_port = 5679;
    int port_tok = thalovant_json_object_get_alias(
        json, toks, count, 0, ALIASES("default_port", "defaultPort", "hub_http_port", "port"), 4);
    if (port_tok >= 0) {
        long port;
        rc = thalovant_json_as_int(json, &toks[port_tok], &port);
        if (rc == THALOVANT_OK) {
            if (port <= 0 || port > 65535) {
                return THALOVANT_ERR_INVALID;
            }
            out->default_port = (int)port;
        } else if (rc != THALOVANT_ERR_MISSING) {
            /* An empty string keeps the default; junk is an error. */
            return THALOVANT_ERR_INVALID;
        }
    }

    if (out->access_key[0] == '\0' || out->password[0] == '\0' || out->site_id[0] == '\0' ||
        out->default_master[0] == '\0') {
        return THALOVANT_ERR_MISSING;
    }

    int mqtt_tok = thalovant_json_object_get(json, toks, count, 0, "mqtt");
    if (mqtt_tok >= 0 && toks[mqtt_tok].type == THALOVANT_JSON_OBJECT) {
        rc = parse_mqtt(json, toks, count, mqtt_tok, &out->mqtt);
        if (rc != THALOVANT_OK) {
            return rc;
        }
    }
    return THALOVANT_OK;
}
