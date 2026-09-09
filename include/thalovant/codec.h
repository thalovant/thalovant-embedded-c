/* Hex and Base64 codecs (lowercase hex, standard Base64 alphabet). */
#ifndef THALOVANT_CODEC_H
#define THALOVANT_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "thalovant/error.h"

/*
 * Every encoder NUL-terminates and returns the number of characters written
 * (excluding the terminator); decoders return the number of bytes written.
 * All functions return THALOVANT_ERR_NOMEM when `cap` is too small and
 * THALOVANT_ERR_INVALID on malformed input. Outputs beyond INT_MAX return
 * THALOVANT_ERR_NOMEM because the result cannot represent their length.
 */

int thalovant_hex_encode(const uint8_t *in, size_t len, char *out, size_t cap);
int thalovant_hex_decode(const char *in, size_t in_len, uint8_t *out, size_t cap);

/* Accepts padded and unpadded Base64; supplied padding must have the
 * correct length and appear only at the end. */
int thalovant_base64_encode(const uint8_t *in, size_t len, char *out, size_t cap);
int thalovant_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t cap);

#endif /* THALOVANT_CODEC_H */
