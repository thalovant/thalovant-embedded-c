#include "thalovant/request_helpers.h"
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int replace(char *out, size_t cap, size_t start, size_t end, const char *value, size_t count)
{
    size_t len = strlen(out);
    if (end > len || start > end || count >= cap - (len - (end - start))) return THALOVANT_ERR_NOMEM;
    memmove(out + start + count, out + end, len - end + 1);
    if (count != 0) memcpy(out + start, value, count);
    return THALOVANT_OK;
}
static bool space(unsigned char c) { return c == 32 || (c >= 9 && c <= 13); }
int thalovant_speakable(const char *pattern, const thalovant_speakable_slot *slots, size_t slot_count, char *out, size_t cap)
{
    if (pattern == NULL || out == NULL || cap == 0 || (slot_count != 0 && slots == NULL)) return THALOVANT_ERR_INVALID;
    size_t len = strlen(pattern);
    if (len >= cap || len > INT_MAX) return THALOVANT_ERR_NOMEM;
    memmove(out, pattern, len + 1);
    for (int phase = 0; phase < 2; phase++) {
        char open = phase == 0 ? '[' : '(', close = phase == 0 ? ']' : ')';
        for (;;) {
            size_t start = 0, end = 0;
            bool found_open = false, found = false;
            for (size_t i = 0; out[i] != '\0'; i++) {
                if (out[i] == open) { start = i; found_open = true; }
                else if (out[i] == close && found_open) { end = i; found = true; break; }
            }
            if (!found) break;
            if (phase == 0) { (void)replace(out, cap, start, end + 1, NULL, 0); continue; }
            size_t first = 0, first_len = 0, real = 0, begin = start + 1;
            bool empty = false;
            for (size_t i = begin; i <= end; i++) {
                if (i != end && out[i] != '|') continue;
                size_t left = begin, right = i;
                while (left < right && space((unsigned char)out[left])) left++;
                while (right > left && space((unsigned char)out[right - 1])) right--;
                if (right == left) empty = true;
                else { if (real == 0) { first = left; first_len = right - left; } real++; }
                begin = i + 1;
            }
            if (real == 0 || (empty && real <= 1)) { (void)replace(out, cap, start, end + 1, NULL, 0); continue; }
            /* Chosen text is inside the removed range: move it before shifting the tail. */
            memmove(out + start, out + first, first_len);
            memmove(out + start + first_len, out + end + 1, strlen(out + end + 1) + 1);
        }
    }
    for (size_t i = 0; out[i] != '\0'; i++) {
        if (out[i] != '{') continue;
        size_t end = i + 1;
        if (!((out[end] >= 'a' && out[end] <= 'z') || out[end] == '_')) continue;
        while ((out[end] >= 'a' && out[end] <= 'z') || (out[end] >= '0' && out[end] <= '9') || out[end] == '_') end++;
        if (out[end] != '}') continue;
        size_t name_len = end - i - 1;
        const char *value = NULL;
        for (size_t s = 0; s < slot_count; s++) {
            if (slots[s].name == NULL || slots[s].value == NULL) return THALOVANT_ERR_INVALID;
            if (strlen(slots[s].name) == name_len && memcmp(slots[s].name, out + i + 1, name_len) == 0) { value = slots[s].value; break; }
        }
        size_t count;
        if (value != NULL) {
            count = strlen(value);
            int rc = replace(out, cap, i, end + 1, value, count); if (rc < 0) return rc;
        } else {
            count = name_len;
            memmove(out + i, out + i + 1, count);
            for (size_t n = 0; n < count; n++) if (out[i + n] == '_') out[i + n] = ' ';
            memmove(out + i + count, out + end + 1, strlen(out + end + 1) + 1);
        }
        if (count == 0) { if (i != 0) i--; else i = (size_t)-1; }
        else i += count - 1;
    }
    size_t written = 0;
    for (size_t i = 0; out[i] != '\0';) {
        size_t end = i;
        while (out[end] != '\0' && space((unsigned char)out[end])) end++;
        if (end - i >= 2) { out[written++] = ' '; i = end; }
        else out[written++] = out[i++];
    }
    out[written] = '\0';
    size_t begin = 0;
    while (out[begin] == ' ' || out[begin] == ',') begin++;
    while (written > begin && (out[written - 1] == ' ' || out[written - 1] == ',')) written--;
    memmove(out, out + begin, written - begin); out[written - begin] = '\0';
    return written - begin > INT_MAX ? THALOVANT_ERR_NOMEM : (int)(written - begin);
}

static int append(char *out, size_t cap, const char *value)
{
    size_t len = strlen(out), count = strlen(value);
    if (count >= cap - len) return THALOVANT_ERR_NOMEM;
    memcpy(out + len, value, count + 1); return THALOVANT_OK;
}
static int quoted(char *out, size_t cap, const char *value, bool uppercase)
{
    if (value == NULL) value = "";
    while (space((unsigned char)*value)) value++;
    size_t len = strlen(value);
    while (len > 0 && space((unsigned char)value[len - 1])) len--;
    int rc = append(out, cap, "\""); if (rc < 0) return rc;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        char piece[7] = {0};
        if (c == '"' || c == '\\') { piece[0] = '\\'; piece[1] = (char)c; }
        else if (c < 32) { (void)snprintf(piece, sizeof(piece), "\\u%04x", c); }
        else piece[0] = (char)(uppercase && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        if ((rc = append(out, cap, piece)) < 0) return rc;
    }
    return append(out, cap, "\"");
}
static bool nonempty(const char *s) { if (s == NULL) return false; while (space((unsigned char)*s)) s++; return *s != '\0'; }
static int number(char *out, size_t cap, double value)
{
    char formatted[128];
    int size = snprintf(formatted, sizeof(formatted), "%.17g", value);
    if (size < 0 || (size_t)size >= sizeof(formatted)) return THALOVANT_ERR_INVALID;
    /* JSON always uses a decimal point even when the caller's locale does not. */
    const char *decimal = localeconv()->decimal_point;
    if (decimal != NULL && decimal[0] != '\0' && strcmp(decimal, ".") != 0) {
        char *point = strstr(formatted, decimal);
        if (point != NULL) { size_t dlen = strlen(decimal); *point = '.'; memmove(point + 1, point + dlen, strlen(point + dlen) + 1); }
    }
    return append(out, cap, formatted);
}
int thalovant_build_location(const thalovant_location *location, char *out, size_t cap)
{
    if (location == NULL || out == NULL || cap == 0) return THALOVANT_ERR_INVALID;
    out[0] = '\0'; if (!nonempty(location->city)) return 0;
    int rc;
#define ADD(value) do { if ((rc = append(out, cap, value)) < 0) return rc; } while (0)
#define QUOTE(value, upper) do { if ((rc = quoted(out, cap, value, upper)) < 0) return rc; } while (0)
    ADD("{\"city\":"); QUOTE(location->city, false);
    if (nonempty(location->region)) { ADD(",\"region\":"); QUOTE(location->region, false); }
    if (nonempty(location->country)) { ADD(",\"country_code\":"); QUOTE(location->country, true); }
    if (nonempty(location->timezone)) { ADD(",\"timezone\":{\"code\":"); QUOTE(location->timezone, false); ADD("}"); }
    double lat = location->latitude, lon = location->longitude;
    if (location->has_coordinates && isfinite(lat) && isfinite(lon) && (lat != 0 || lon != 0) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
        ADD(",\"coordinate\":{\"latitude\":"); if ((rc = number(out, cap, lat)) < 0) return rc;
        ADD(",\"longitude\":"); if ((rc = number(out, cap, lon)) < 0) return rc; ADD("}");
    }
    ADD("}");
#undef ADD
#undef QUOTE
    size_t length = strlen(out); return length > INT_MAX ? THALOVANT_ERR_NOMEM : (int)length;
}
