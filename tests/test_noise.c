#include "fixtures/noise-node.h"
#include "harness.h"
#include "thalovant/aes_gcm.h"
#include "thalovant/noise.h"
#include <stdlib.h>

static size_t unhex(const char *text, uint8_t *out)
{
    size_t n = strlen(text) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        CHECK_INT_EQ(sscanf(text + 2 * i, "%2x", &v), 1);
        out[i] = (uint8_t)v;
    }
    return n;
}
static void equals_hex(const uint8_t *actual, size_t len, const char *expected)
{
    uint8_t decoded[2048];
    size_t n = unhex(expected, decoded);
    CHECK_INT_EQ(len, n);
    if (len == n)
        CHECK(memcmp(actual, decoded, n) == 0);
}
static void primitives(void)
{
    uint8_t priv[32], pub[32], key[32] = {0}, nonce[12] = {0}, plain[16] = {0}, ct[16], tag[16],
                               expected[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", priv);
    CHECK_INT_EQ(thalovant_x25519_public(priv, pub), 0);
    equals_hex(pub, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    memset(pub, 0, 32);
    CHECK_INT_EQ(thalovant_x25519(priv, pub, expected), THALOVANT_ERR_AUTH);
    /* NIST GCM AES256 zero key/IV, empty AAD and one zero block. */
    CHECK_INT_EQ(thalovant_aes256_gcm_encrypt(key, nonce, 12, NULL, 0, plain, 16, ct, tag), 0);
    equals_hex(ct, 16, "cea7403d4d606b6e074ec5d3baf39d18");
    equals_hex(tag, 16, "d0d1c8a799996bf0265b98b5d48ab919");
    CHECK_INT_EQ(thalovant_aes256_gcm_decrypt(key, nonce, 12, NULL, 0, ct, 16, tag, plain), 0);
    tag[0] ^= 1;
    memset(plain, 0xa5, 16);
    CHECK_INT_EQ(thalovant_aes256_gcm_decrypt(key, nonce, 12, NULL, 0, ct, 16, tag, plain),
                 THALOVANT_ERR_AUTH);
    CHECK(plain[0] == 0xa5);
    /* Host test allocates the caller scratch, never a core path. */
    uint64_t *scratch = malloc(THALOVANT_NOISE_PSK_MEMORY_WORDS * sizeof(uint64_t));
    CHECK(scratch != NULL);
    if (scratch) {
        CHECK_INT_EQ(thalovant_noise_psk((const uint8_t *)"fixture-password", 16,
                                         (const uint8_t *)"fixture-hub", 11, scratch,
                                         THALOVANT_NOISE_PSK_MEMORY_WORDS, pub),
                     0);
        equals_hex(pub, 32, fixture_psk);
        uint64_t all = 0;
        for (size_t i = 0; i < THALOVANT_NOISE_PSK_MEMORY_WORDS; i++)
            all |= scratch[i];
        CHECK(all == 0);
        CHECK_INT_EQ(thalovant_noise_psk(NULL, 0, NULL, 0, scratch, 1, pub), THALOVANT_ERR_NOMEM);
        free(scratch);
    }
}
static void start(thalovant_noise *s, int pattern, int initiator, int wrong_pin)
{
    uint8_t psk[32], local[32], ephemeral[32], pin[32], prologue[2048];
    unhex(fixture_psk, psk);
    unhex(initiator ? fixture_static_i : fixture_static_r, local);
    unhex(initiator ? fixture_ephemeral_i : fixture_ephemeral_r, ephemeral);
    unhex(initiator ? fixture_public_r : fixture_public_i, pin);
    if (wrong_pin)
        pin[0] ^= 1;
    size_t n = unhex(noise_vectors[pattern - 1].prologue, prologue);
    CHECK_INT_EQ(
        thalovant_noise_init(s, pattern, initiator, psk, prologue, n, local, ephemeral, pin), 0);
}
static void exchange(thalovant_noise *s, int pattern, int initiator)
{
    const noise_vector *v = &noise_vectors[pattern - 1];
    uint8_t message[2048], out[2048];
    size_t n, written;
    start(s, pattern, initiator, 0);
    for (int step = 0; step < (pattern == THALOVANT_NOISE_XX ? 3 : 2); step++) {
        CHECK(!s->ready);
        if ((step % 2 == 0) == initiator) {
            CHECK_INT_EQ(thalovant_noise_write(s, (const uint8_t *)v->payloads[step],
                                               strlen(v->payloads[step]), out, sizeof out,
                                               &written),
                         0);
            equals_hex(out, written, v->messages[step]);
        } else {
            n = unhex(v->messages[step], message);
            CHECK_INT_EQ(thalovant_noise_read(s, message, n, out, sizeof out, &written), 0);
            CHECK_INT_EQ(written, strlen(v->payloads[step]));
            CHECK(memcmp(out, v->payloads[step], written) == 0);
        }
    }
    CHECK(s->ready);
    equals_hex(s->remote_static, 32, initiator ? fixture_public_r : fixture_public_i);
}
static void transcripts(void)
{
    for (int pattern = 1; pattern <= 2; pattern++)
        for (int initiator = 0; initiator <= 1; initiator++) {
            thalovant_noise s;
            const noise_vector *v = &noise_vectors[pattern - 1];
            uint8_t plain[2048], sealed[2048], out[2048];
            size_t n, written;
            exchange(&s, pattern, initiator);
            for (int i = 0; i < 3; i++) {
                plain[0] = 0;
                memcpy(plain + 1, v->plaintexts[i], strlen(v->plaintexts[i]));
                n = strlen(v->plaintexts[i]) + 1;
                CHECK_INT_EQ(thalovant_noise_encrypt(&s, plain, n, sealed, sizeof sealed, &written),
                             0);
                equals_hex(sealed, written, initiator ? v->transport_i[i] : v->transport_r[i]);
                size_t ct = unhex(initiator ? v->transport_r[i] : v->transport_i[i], sealed);
                CHECK_INT_EQ(thalovant_noise_decrypt(&s, sealed, ct, out, sizeof out, &written), 0);
                CHECK_INT_EQ(written, n);
                CHECK(memcmp(out, plain, n) == 0);
            }
            /* Replaying a valid frame terminates the session and zeros its keys. */
            n = unhex(initiator ? v->transport_r[0] : v->transport_i[0], sealed);
            CHECK_INT_EQ(thalovant_noise_decrypt(&s, sealed, n, out, sizeof out, &written),
                         THALOVANT_ERR_AUTH);
            CHECK(s.failed && !s.ready);
            CHECK(s.send_key[0] == 0);
            CHECK_INT_EQ(thalovant_noise_encrypt(&s, plain, 2, sealed, sizeof sealed, &written),
                         THALOVANT_ERR_INVALID);
            /* Re-initialization starts a distinct cipher state; caller supplies fresh
             * entropy in production. Reusing vector entropy here is test-only. */
            exchange(&s, pattern, initiator);
            s.send_nonce = UINT64_MAX;
            CHECK_INT_EQ(thalovant_noise_encrypt(&s, plain, 2, sealed, sizeof sealed, &written),
                         THALOVANT_ERR_AUTH);
            CHECK(s.failed);
        }
}
static void failures(void)
{
    const char *patterns[] = {"XXpsk2", "KKpsk0"},
               *suites[] = {"25519_ChaChaPoly_SHA256", THALOVANT_NOISE_SUITE};
    int selected = 0;
    CHECK_INT_EQ(thalovant_noise_select(patterns, 2, suites, 2, 0, &selected), 0);
    CHECK_INT_EQ(selected, 1);
    CHECK_INT_EQ(thalovant_noise_select(patterns, 2, suites, 2, 1, &selected), 0);
    CHECK_INT_EQ(selected, 2);
    CHECK_INT_EQ(thalovant_noise_select(patterns, 2, suites, 1, 0, &selected),
                 THALOVANT_ERR_UNSUPPORTED);
    CHECK_INT_EQ(thalovant_noise_select(patterns + 1, 1, suites, 2, 0, &selected),
                 THALOVANT_ERR_UNSUPPORTED);
    thalovant_noise s;
    uint8_t message[2048], out[2048];
    size_t n, written;
    start(&s, 1, 1, 0);
    CHECK_INT_EQ(thalovant_noise_encrypt(&s, (const uint8_t *)"\0x", 2, out, sizeof out, &written),
                 THALOVANT_ERR_INVALID);
    CHECK(s.failed);
    start(&s, 1, 1, 0);
    CHECK_INT_EQ(thalovant_noise_read(&s, out, 1, message, sizeof message, &written),
                 THALOVANT_ERR_INVALID);
    start(&s, 1, 1, 0);
    CHECK_INT_EQ(thalovant_noise_write(&s, NULL, 0, out, 1, &written), THALOVANT_ERR_NOMEM);
    start(&s, 1, 1, 0);
    CHECK_INT_EQ(thalovant_noise_write(&s, (const uint8_t *)noise_vectors[0].payloads[0],
                                       strlen(noise_vectors[0].payloads[0]), out, sizeof out,
                                       &written),
                 0);
    n = unhex(noise_vectors[0].messages[1], message);
    message[n - 1] ^= 1;
    CHECK_INT_EQ(thalovant_noise_read(&s, message, n, out, sizeof out, &written),
                 THALOVANT_ERR_AUTH);
    CHECK(s.failed);
    start(&s, 1, 1, 1);
    CHECK_INT_EQ(thalovant_noise_write(&s, (const uint8_t *)noise_vectors[0].payloads[0],
                                       strlen(noise_vectors[0].payloads[0]), out, sizeof out,
                                       &written),
                 0);
    n = unhex(noise_vectors[0].messages[1], message);
    CHECK_INT_EQ(thalovant_noise_read(&s, message, n, out, sizeof out, &written),
                 THALOVANT_ERR_AUTH);
    CHECK(s.failed);
    exchange(&s, 1, 1);
    out[0] = 4;
    CHECK_INT_EQ(thalovant_noise_encrypt(&s, out, 1, message, sizeof message, &written),
                 THALOVANT_ERR_INVALID);
    /* Valid chunk sequence is encrypted/decrypted by opposite endpoints. */
    thalovant_noise peer;
    exchange(&s, 1, 1);
    exchange(&peer, 1, 0);
    for (unsigned i = 0; i < 3; i++) {
        uint8_t p[2] = {(uint8_t)(i == 0 ? 2 : i == 1 ? 4 : 5), 'x'};
        CHECK_INT_EQ(thalovant_noise_encrypt(&s, p, 2, message, sizeof message, &written), 0);
        n = written;
        CHECK_INT_EQ(thalovant_noise_decrypt(&peer, message, n, out, sizeof out, &written), 0);
        CHECK(written == 2 && memcmp(p, out, 2) == 0);
    }
    CHECK(!peer.receive_chunked && !s.send_chunked);
}
void tlv_test_noise(void)
{
    primitives();
    transcripts();
    failures();
}
