#include <stdint.h>

#include "harness.h"
#include "thalovant/codec.h"

static void test_hex_round_trip(void)
{
    const uint8_t bytes[] = { 0x00, 0x01, 0xab, 0xff };
    char hex[16];
    CHECK_INT_EQ(thalovant_hex_encode(bytes, sizeof(bytes), hex, sizeof(hex)), 8);
    CHECK_STR_EQ(hex, "0001abff");
    uint8_t back[4];
    CHECK_INT_EQ(thalovant_hex_decode(hex, 8, back, sizeof(back)), 4);
    CHECK(memcmp(back, bytes, 4) == 0);
    CHECK_INT_EQ(thalovant_hex_decode("ABCD", 4, back, sizeof(back)), 2);
    CHECK(back[0] == 0xab && back[1] == 0xcd);
    CHECK_INT_EQ(thalovant_hex_decode("abc", 3, back, sizeof(back)), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_hex_decode("zz", 2, back, sizeof(back)), THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_hex_encode(bytes, sizeof(bytes), hex, 8), THALOVANT_ERR_NOMEM);
}

static void test_base64_vectors(void)
{
    /* RFC 4648 test vectors. */
    const struct {
        const char *plain;
        const char *encoded;
    } cases[] = {
        { "", "" },         { "f", "Zg==" },     { "fo", "Zm8=" },
        { "foo", "Zm9v" },  { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" },
        { "foobar", "Zm9vYmFy" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char encoded[16];
        int len = thalovant_base64_encode((const uint8_t *)cases[i].plain,
                                          strlen(cases[i].plain), encoded, sizeof(encoded));
        CHECK(len >= 0);
        CHECK_STR_EQ(encoded, cases[i].encoded);
        uint8_t decoded[16];
        int bytes = thalovant_base64_decode(cases[i].encoded, strlen(cases[i].encoded), decoded,
                                            sizeof(decoded));
        CHECK_INT_EQ(bytes, (int)strlen(cases[i].plain));
        CHECK(memcmp(decoded, cases[i].plain, (size_t)bytes) == 0);
    }
    /* Unpadded input is accepted too. */
    uint8_t decoded[8];
    CHECK_INT_EQ(thalovant_base64_decode("Zm9vYg", 6, decoded, sizeof(decoded)), 4);
    CHECK(memcmp(decoded, "foob", 4) == 0);
    CHECK_INT_EQ(thalovant_base64_decode("Z", 1, decoded, sizeof(decoded)),
                 THALOVANT_ERR_INVALID);
    CHECK_INT_EQ(thalovant_base64_decode("Zm 9v", 5, decoded, sizeof(decoded)),
                 THALOVANT_ERR_INVALID);
}

void tlv_test_codec(void)
{
    test_hex_round_trip();
    test_base64_vectors();
}
