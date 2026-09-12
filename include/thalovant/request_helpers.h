#ifndef THALOVANT_REQUEST_HELPERS_H
#define THALOVANT_REQUEST_HELPERS_H
#include <stddef.h>
#include <stdbool.h>
#include "thalovant/error.h"

typedef struct { const char *name; const char *value; } thalovant_speakable_slot;
/* Render one intent pattern. Caller owns out; slots must not overlap it.
 * Returns byte length or a negative error. No allocation. When ranking rendered
 * examples, preserve whether each original pattern had a slot; deduplicate
 * equal rendered sentences using the best original rank before limiting. */
int thalovant_speakable(const char *pattern, const thalovant_speakable_slot *slots,
    size_t slot_count, char *out, size_t cap);

typedef struct {
    const char *city;
    const char *region;
    const char *country;
    const char *timezone;
    bool has_coordinates;
    double latitude;
    double longitude;
} thalovant_location;
/* OVOS request-level location JSON. Empty city returns 0 and an empty output.
 * Invalid/absent or zero/zero coordinates are omitted. Strings are trimmed. */
int thalovant_build_location(const thalovant_location *location, char *out, size_t cap);
#endif
