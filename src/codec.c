#include "thalovant/codec.h"

static const char HEX_DIGITS[] = "0123456789abcdef";
static const char B64_DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int thalovant_hex_encode(const uint8_t *in, size_t len, char *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    if (cap < len * 2 + 1) {
        return THALOVANT_ERR_NOMEM;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = HEX_DIGITS[in[i] >> 4];
        out[i * 2 + 1] = HEX_DIGITS[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
    return (int)(len * 2);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int thalovant_hex_decode(const char *in, size_t in_len, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL || in_len % 2 != 0) {
        return THALOVANT_ERR_INVALID;
    }
    size_t bytes = in_len / 2;
    if (cap < bytes) {
        return THALOVANT_ERR_NOMEM;
    }
    for (size_t i = 0; i < bytes; i++) {
        int hi = hex_nibble(in[i * 2]);
        int lo = hex_nibble(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return THALOVANT_ERR_INVALID;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)bytes;
}

int thalovant_base64_encode(const uint8_t *in, size_t len, char *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    size_t out_len = ((len + 2) / 3) * 4;
    if (cap < out_len + 1) {
        return THALOVANT_ERR_NOMEM;
    }
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t chunk = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64_DIGITS[(chunk >> 18) & 0x3f];
        out[o++] = B64_DIGITS[(chunk >> 12) & 0x3f];
        out[o++] = B64_DIGITS[(chunk >> 6) & 0x3f];
        out[o++] = B64_DIGITS[chunk & 0x3f];
        i += 3;
    }
    size_t rest = len - i;
    if (rest == 1) {
        uint32_t chunk = (uint32_t)in[i] << 16;
        out[o++] = B64_DIGITS[(chunk >> 18) & 0x3f];
        out[o++] = B64_DIGITS[(chunk >> 12) & 0x3f];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rest == 2) {
        uint32_t chunk = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64_DIGITS[(chunk >> 18) & 0x3f];
        out[o++] = B64_DIGITS[(chunk >> 12) & 0x3f];
        out[o++] = B64_DIGITS[(chunk >> 6) & 0x3f];
        out[o++] = '=';
    }
    out[o] = '\0';
    return (int)o;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int thalovant_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return THALOVANT_ERR_INVALID;
    }
    while (in_len > 0 && in[in_len - 1] == '=') {
        in_len--;
    }
    if (in_len % 4 == 1) {
        return THALOVANT_ERR_INVALID;
    }
    size_t bytes = (in_len / 4) * 3;
    switch (in_len % 4) {
    case 2: bytes += 1; break;
    case 3: bytes += 2; break;
    default: break;
    }
    if (cap < bytes) {
        return THALOVANT_ERR_NOMEM;
    }
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int value = b64_value(in[i]);
        if (value < 0) {
            return THALOVANT_ERR_INVALID;
        }
        acc = (acc << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)((acc >> bits) & 0xff);
        }
    }
    return (int)o;
}
