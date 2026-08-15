#include "thalovant/topics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

    /*
     * `topic_prefix` is the full base "hivemind/<hub-id>/<access-key>"
     * (plaintext, no hashing). Each channel is a plain suffix on it:
     *   publish requests  -> "<prefix>/in"
     *   subscribe replies -> "<prefix>/out"
     *   retained presence -> "<prefix>/status"
     */
    char prefix[THALOVANT_MQTT_TOPIC_PREFIX_MAX];
    int rc = trim_slashes(mqtt->topic_prefix, prefix, sizeof(prefix));
    if (rc != THALOVANT_OK) {
        return rc;
    }
    /*
     * Strip any surrounding whitespace the slash-trim left in place so a
     * padded, empty, or whitespace-only prefix normalizes the same way as the
     * Node/Go SDKs before it is validated below.
     */
    size_t len = strlen(prefix);
    size_t start = 0;
    while (start < len && isspace((unsigned char)prefix[start])) {
        start++;
    }
    while (len > start && isspace((unsigned char)prefix[len - 1])) {
        len--;
    }
    len -= start;
    memmove(prefix, prefix + start, len);
    prefix[len] = '\0';
    if (len == 0) {
        return THALOVANT_ERR_MISSING;
    }
    /*
     * A concrete publish/subscribe base must never carry an MQTT wildcard
     * ('#' or '+') or a control character (incl. an embedded NUL): such a
     * prefix would let a malformed identity subscribe far beyond its own
     * channels or smuggle a terminator into the derived topics.
     */
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)prefix[i];
        if (c == '#' || c == '+' || c < 0x20) {
            return THALOVANT_ERR_INVALID;
        }
    }

    const struct {
        char *dst;
        size_t cap;
        const char *suffix;
    } channels[] = {
        { out->inbound, sizeof(out->inbound), "in" },
        { out->outbound, sizeof(out->outbound), "out" },
        { out->status, sizeof(out->status), "status" },
    };
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
        int written = snprintf(channels[i].dst, channels[i].cap, "%s/%s", prefix,
                               channels[i].suffix);
        if (written < 0 || (size_t)written >= channels[i].cap) {
            return THALOVANT_ERR_NOMEM;
        }
    }
    return THALOVANT_OK;
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
