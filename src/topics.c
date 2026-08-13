#include "thalovant/topics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "thalovant/codec.h"
#include "thalovant/sha256.h"

static int copy_str(char *out, size_t cap, const char *in)
{
    size_t len = strlen(in);
    if (len + 1 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out, in, len + 1);
    return THALOVANT_OK;
}

/* Trim leading and trailing '/' into `out`. */
static int trim_slashes(const char *in, char *out, size_t cap)
{
    size_t len = strlen(in);
    size_t start = 0;
    while (start < len && in[start] == '/') {
        start++;
    }
    size_t end = len;
    while (end > start && in[end - 1] == '/') {
        end--;
    }
    size_t n = end - start;
    if (n + 1 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out, in + start, n);
    out[n] = '\0';
    return THALOVANT_OK;
}

/*
 * Find the earliest of "/c2s/", "/s2c/", "/status/" in `topic` and replace
 * it with "/<segment>/" (mirrors `siblingMqttTopic`). Copies verbatim when
 * no marker exists.
 */
static int sibling_topic(const char *topic, const char *segment, char *out, size_t cap)
{
    static const char *const MARKERS[] = { "/c2s/", "/s2c/", "/status/" };
    const char *found = NULL;
    size_t found_len = 0;
    for (size_t i = 0; i < sizeof(MARKERS) / sizeof(MARKERS[0]); i++) {
        const char *hit = strstr(topic, MARKERS[i]);
        if (hit != NULL && (found == NULL || hit < found)) {
            found = hit;
            found_len = strlen(MARKERS[i]);
        }
    }
    if (found == NULL) {
        return copy_str(out, cap, topic);
    }
    size_t head = (size_t)(found - topic);
    int written = snprintf(out, cap, "%.*s/%s/%s", (int)head, topic, segment,
                           found + found_len);
    if (written < 0 || (size_t)written >= cap) {
        return THALOVANT_ERR_NOMEM;
    }
    return THALOVANT_OK;
}

/* Join `topic_prefix` segments (dropping empties) into base; returns the
 * last segment's offset within `base`, or -1 when base is empty. */
static int normalize_base(const char *raw, char *base, size_t cap, int *last_segment)
{
    size_t out = 0;
    int last = -1;
    size_t i = 0;
    size_t len = strlen(raw);
    while (i < len) {
        while (i < len && raw[i] == '/') {
            i++;
        }
        size_t start = i;
        while (i < len && raw[i] != '/') {
            i++;
        }
        size_t seg = i - start;
        if (seg == 0) {
            continue;
        }
        if (out + seg + 2 > cap) {
            return THALOVANT_ERR_NOMEM;
        }
        if (out > 0) {
            base[out++] = '/';
        }
        last = (int)out;
        memcpy(base + out, raw + start, seg);
        out += seg;
    }
    base[out] = '\0';
    *last_segment = last;
    return THALOVANT_OK;
}

static bool base_has_segment(const char *base, const char *segment)
{
    size_t seg_len = strlen(segment);
    const char *cursor = base;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '/');
        size_t n = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        if (n == seg_len && strncmp(cursor, segment, n) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
    }
    return false;
}

static int build_topic(char *out, size_t cap, const char *base, const char *kind,
                       const char *satellite_id)
{
    int written = snprintf(out, cap, "%s/%s/%s", base, kind, satellite_id);
    if (written < 0 || (size_t)written >= cap) {
        return THALOVANT_ERR_NOMEM;
    }
    return THALOVANT_OK;
}

int thalovant_mqtt_topics_derive(const thalovant_identity *identity, thalovant_mqtt_topics *out)
{
    if (identity == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    const thalovant_mqtt_credentials *mqtt = &identity->mqtt;
    if (!mqtt->present) {
        return THALOVANT_ERR_MISSING;
    }
    memset(out, 0, sizeof(*out));

    char satellite_id[THALOVANT_ACCESS_KEY_MAX];
    if (mqtt->hash_topics) {
        uint8_t digest[32];
        thalovant_sha256((const uint8_t *)identity->access_key, strlen(identity->access_key),
                         digest);
        char hex[17];
        int rc = thalovant_hex_encode(digest, 8, hex, sizeof(hex));
        if (rc < 0) {
            return rc;
        }
        memcpy(satellite_id, hex, 17);
    } else {
        int rc = copy_str(satellite_id, sizeof(satellite_id), identity->access_key);
        if (rc != THALOVANT_OK) {
            return rc;
        }
    }

    /* 1. Explicit topics win. */
    if (mqtt->c2s_topic[0] != '\0' && mqtt->s2c_topic[0] != '\0') {
        int rc = copy_str(out->c2s, sizeof(out->c2s), mqtt->c2s_topic);
        if (rc != THALOVANT_OK) return rc;
        rc = copy_str(out->s2c, sizeof(out->s2c), mqtt->s2c_topic);
        if (rc != THALOVANT_OK) return rc;
        if (mqtt->status_topic[0] != '\0') {
            return copy_str(out->status, sizeof(out->status), mqtt->status_topic);
        }
        return sibling_topic(mqtt->c2s_topic, "status", out->status, sizeof(out->status));
    }

    char base[THALOVANT_TOPIC_MAX] = "";
    char raw[THALOVANT_MQTT_TOPIC_PREFIX_MAX];
    int rc = trim_slashes(mqtt->topic_prefix, raw, sizeof(raw));
    if (rc != THALOVANT_OK) {
        return rc;
    }

    if (raw[0] != '\0') {
        /* 2. A prefix that already names a c2s/s2c/status topic. */
        const char *kinds[] = { "/c2s/", "/s2c/", "/status/" };
        char *slots[3];
        for (int i = 0; i < 3; i++) {
            slots[i] = i == 0 ? out->c2s : (i == 1 ? out->s2c : out->status);
        }
        for (int i = 0; i < 3; i++) {
            if (strstr(raw, kinds[i]) != NULL) {
                rc = copy_str(slots[i], THALOVANT_TOPIC_MAX, raw);
                if (rc != THALOVANT_OK) return rc;
                const char *segments[] = { "c2s", "s2c", "status" };
                for (int j = 0; j < 3; j++) {
                    if (j == i) continue;
                    rc = sibling_topic(raw, segments[j], slots[j], THALOVANT_TOPIC_MAX);
                    if (rc != THALOVANT_OK) return rc;
                }
                return THALOVANT_OK;
            }
        }
        /* 3. Base prefix: drop a trailing satellite segment, append hub. */
        int last = -1;
        rc = normalize_base(raw, base, sizeof(base), &last);
        if (rc != THALOVANT_OK) {
            return rc;
        }
        if (last >= 0) {
            const char *tail = base + last;
            if (strcmp(tail, identity->access_key) == 0 || strcmp(tail, mqtt->username) == 0 ||
                strcmp(tail, satellite_id) == 0) {
                base[last > 0 ? last - 1 : 0] = '\0';
            }
        }
        char hub[THALOVANT_MQTT_HUB_ID_MAX];
        rc = trim_slashes(mqtt->hub_id, hub, sizeof(hub));
        if (rc != THALOVANT_OK) {
            return rc;
        }
        if (hub[0] != '\0' && !base_has_segment(base, hub)) {
            /* Node always joins with '/', even onto an empty base. */
            size_t base_len = strlen(base);
            int written = snprintf(base + base_len, sizeof(base) - base_len, "/%s", hub);
            if (written < 0 || (size_t)written >= sizeof(base) - base_len) {
                return THALOVANT_ERR_NOMEM;
            }
        }
    } else if (mqtt->hub_id[0] != '\0') {
        /* 4. hub_id alone. */
        char hub[THALOVANT_MQTT_HUB_ID_MAX];
        rc = trim_slashes(mqtt->hub_id, hub, sizeof(hub));
        if (rc != THALOVANT_OK) {
            return rc;
        }
        int written = snprintf(base, sizeof(base), "hivemind/%s", hub);
        if (written < 0 || (size_t)written >= sizeof(base)) {
            return THALOVANT_ERR_NOMEM;
        }
    }

    if (base[0] == '\0') {
        return THALOVANT_ERR_MISSING;
    }
    rc = build_topic(out->c2s, sizeof(out->c2s), base, "c2s", satellite_id);
    if (rc != THALOVANT_OK) return rc;
    rc = build_topic(out->s2c, sizeof(out->s2c), base, "s2c", satellite_id);
    if (rc != THALOVANT_OK) return rc;
    return build_topic(out->status, sizeof(out->status), base, "status", satellite_id);
}

int thalovant_mqtt_endpoint(const thalovant_mqtt_credentials *mqtt, char *out, size_t cap)
{
    if (mqtt == NULL || out == NULL || mqtt->endpoint[0] == '\0') {
        return THALOVANT_ERR_INVALID;
    }
    const char *endpoint = mqtt->endpoint;
    int written;
    if (mqtt->tls && strncmp(endpoint, "mqtt://", 7) == 0) {
        written = snprintf(out, cap, "mqtts://%s", endpoint + 7);
    } else {
        written = snprintf(out, cap, "%s", endpoint);
    }
    if (written < 0 || (size_t)written >= cap) {
        return THALOVANT_ERR_NOMEM;
    }
    size_t len = (size_t)written;
    if (len > 0 && out[len - 1] == '/') {
        out[--len] = '\0';
    }
    return (int)len;
}

int thalovant_mqtt_client_id(const char *access_key, char *out, size_t cap)
{
    if (access_key == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    static const char PREFIX[] = "thalovant-";
    size_t prefix_len = sizeof(PREFIX) - 1;
    size_t key_len = strlen(access_key);
    if (key_len == 0) {
        return THALOVANT_ERR_MISSING;
    }
    if (key_len > 48) {
        key_len = 48;
    }
    if (prefix_len + key_len + 1 > cap) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out, PREFIX, prefix_len);
    for (size_t i = 0; i < key_len; i++) {
        char c = access_key[i];
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
        out[prefix_len + i] = safe ? c : '-';
    }
    out[prefix_len + key_len] = '\0';
    return (int)(prefix_len + key_len);
}

int thalovant_endpoint_parse(const char *url, thalovant_endpoint *out)
{
    if (url == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    const char *sep = strstr(url, "://");
    if (sep == NULL || sep == url) {
        return THALOVANT_ERR_INVALID;
    }
    size_t scheme_len = (size_t)(sep - url);
    if (scheme_len + 1 > sizeof(out->scheme)) {
        return THALOVANT_ERR_NOMEM;
    }
    for (size_t i = 0; i < scheme_len; i++) {
        out->scheme[i] = (char)tolower((unsigned char)url[i]);
    }
    out->scheme[scheme_len] = '\0';

    const char *cursor = sep + 3;
    const char *host_start;
    const char *host_end;
    if (*cursor == '[') {
        host_start = cursor + 1;
        const char *close = strchr(host_start, ']');
        if (close == NULL) {
            return THALOVANT_ERR_INVALID;
        }
        host_end = close;
        cursor = close + 1;
    } else {
        host_start = cursor;
        while (*cursor != '\0' && *cursor != ':' && *cursor != '/' && *cursor != '?') {
            cursor++;
        }
        host_end = cursor;
    }
    size_t host_len = (size_t)(host_end - host_start);
    if (host_len == 0) {
        return THALOVANT_ERR_INVALID;
    }
    if (host_len + 1 > sizeof(out->host)) {
        return THALOVANT_ERR_NOMEM;
    }
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    long port = 0;
    if (*cursor == ':') {
        cursor++;
        if (!isdigit((unsigned char)*cursor)) {
            return THALOVANT_ERR_INVALID;
        }
        while (isdigit((unsigned char)*cursor)) {
            port = port * 10 + (*cursor - '0');
            if (port > 65535) {
                return THALOVANT_ERR_INVALID;
            }
            cursor++;
        }
    }
    if (*cursor != '\0') {
        if (*cursor != '/' && *cursor != '?') {
            return THALOVANT_ERR_INVALID;
        }
        size_t path_len = strlen(cursor);
        if (path_len + 1 > sizeof(out->path)) {
            return THALOVANT_ERR_NOMEM;
        }
        memcpy(out->path, cursor, path_len + 1);
    }

    out->tls = strcmp(out->scheme, "mqtts") == 0 || strcmp(out->scheme, "wss") == 0 ||
               strcmp(out->scheme, "https") == 0 || strcmp(out->scheme, "ssl") == 0;
    if (port == 0) {
        if (strcmp(out->scheme, "mqtt") == 0) port = 1883;
        else if (strcmp(out->scheme, "mqtts") == 0) port = 8883;
        else if (strcmp(out->scheme, "ws") == 0 || strcmp(out->scheme, "http") == 0) port = 80;
        else if (strcmp(out->scheme, "wss") == 0 || strcmp(out->scheme, "https") == 0) port = 443;
    }
    out->port = (uint16_t)port;
    return THALOVANT_OK;
}
