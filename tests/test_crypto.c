#include <stdint.h>

#include "harness.h"
#include "thalovant/aes_gcm.h"
#include "thalovant/codec.h"
#include "thalovant/sha256.h"

static void from_hex(const char *hex, uint8_t *out, size_t cap)
{
    int rc = thalovant_hex_decode(hex, strlen(hex), out, cap);
    CHECK(rc >= 0);
}

static void check_hex(const uint8_t *bytes, size_t len, const char *expected)
{
    char hex[256];
    CHECK(thalovant_hex_encode(bytes, len, hex, sizeof(hex)) >= 0);
    CHECK_STR_EQ(hex, expected);
}

static void test_sha256(void)
{
    uint8_t digest[32];
    thalovant_sha256((const uint8_t *)"abc", 3, digest);
    check_hex(digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    thalovant_sha256((const uint8_t *)"", 0, digest);
    check_hex(digest, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    /* 56-byte message exercises the two-block padding path. */
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    thalovant_sha256((const uint8_t *)msg, strlen(msg), digest);
    check_hex(digest, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/* NIST SP 800-38D / GCM spec test cases 1-4 (AES-128, 12-byte IV). */
static void test_nist_vectors(void)
{
    uint8_t key[16] = { 0 };
    uint8_t iv[12] = { 0 };
    uint8_t tag[16];
    /* Case 1: empty plaintext. */
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key, iv, sizeof(iv), NULL, 0, NULL, 0, NULL, tag),
                 THALOVANT_OK);
    check_hex(tag, 16, "58e2fccefa7e3061367f1d57a4e7455a");
    /* Case 2: one zero block. */
    uint8_t plaintext16[16] = { 0 };
    uint8_t ciphertext[64];
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key, iv, sizeof(iv), NULL, 0, plaintext16, 16,
                                           ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, 16, "0388dace60b6a392f328c2b971b2fe78");
    check_hex(tag, 16, "ab6e47d42cec13bdf53a67b21257bddf");
    /* Case 3: 64-byte plaintext. */
    uint8_t key3[16], iv3[12], plaintext3[64];
    from_hex("feffe9928665731c6d6a8f9467308308", key3, sizeof(key3));
    from_hex("cafebabefacedbaddecaf888", iv3, sizeof(iv3));
    from_hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
             "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
             plaintext3, sizeof(plaintext3));
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key3, iv3, sizeof(iv3), NULL, 0, plaintext3, 64,
                                           ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, 64,
              "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
              "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985");
    check_hex(tag, 16, "4d5c2af327cd64a62cf35abd2ba6fab4");
    /* Case 4: 60-byte plaintext with AAD. */
    uint8_t aad[20];
    from_hex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, sizeof(aad));
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key3, iv3, sizeof(iv3), aad, sizeof(aad), plaintext3,
                                           60, ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, 60,
              "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
              "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091");
    check_hex(tag, 16, "5bc94fbc3221a5db94fae95ae7121a47");
    /* And round-trip through decrypt. */
    uint8_t recovered[64];
    CHECK_INT_EQ(thalovant_aes_gcm_decrypt(key3, iv3, sizeof(iv3), aad, sizeof(aad), ciphertext,
                                           60, tag, recovered),
                 THALOVANT_OK);
    CHECK(memcmp(recovered, plaintext3, 60) == 0);
}

/*
 * Known-answer vectors generated with the local `node` binary
 * (crypto.createCipheriv "aes-128-gcm" with a 16-byte IV) — the exact
 * configuration the Node SDK uses on the HiveMind wire. Proves interop.
 */
static void test_node_interop_vectors(void)
{
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    uint8_t nonce[16];
    from_hex("000102030405060708090a0b0c0d0e0f", nonce, sizeof(nonce));
    const char *plaintext = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\"}}";
    uint8_t ciphertext[128];
    uint8_t tag[16];
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key, nonce, 16, NULL, 0, (const uint8_t *)plaintext,
                                           strlen(plaintext), ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, strlen(plaintext),
              "eecae278f07b4787d3b62d79b68abd4a8ff45bb2414001531bef052e4bc58e18"
              "78e780fe92d226f6597a3fda42");
    check_hex(tag, 16, "9df782125f2077b8d91c96d9efecaff0");

    const uint8_t *key2 = (const uint8_t *)"thalovant-key-42";
    uint8_t nonce2[16];
    from_hex("f0e1d2c3b4a5968778695a4b3c2d1e0f", nonce2, sizeof(nonce2));
    const char *plaintext2 = "{\"msg_type\":\"hello\",\"payload\":{\"pubkey\":\"\",\"session\":"
                             "{\"session_id\":\"s-1\"},\"site_id\":\"kitchen\"}}";
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key2, nonce2, 16, NULL, 0, (const uint8_t *)plaintext2,
                                           strlen(plaintext2), ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, strlen(plaintext2),
              "4fb6cb946228c3031c246d3a855b1658ccb982033f6f5d8f7ae933c12579f597"
              "f667ffe206ef7e3cbd3985d2269e6e638e56fa1273dfd704428e15aaba09c667"
              "9d884a97708a162ae381e2098d4ec9d84b40352e517bcde41dc535923021b7");
    check_hex(tag, 16, "886ba6d00dbcc9ab43e81c21bad7e679");

    /* 12-byte legacy nonce KAT, also node-generated. */
    uint8_t nonce12[12];
    from_hex("000102030405060708090a0b", nonce12, sizeof(nonce12));
    const char *plaintext3 = "legacy nonce payload";
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key, nonce12, 12, NULL, 0,
                                           (const uint8_t *)plaintext3, strlen(plaintext3),
                                           ciphertext, tag),
                 THALOVANT_OK);
    check_hex(ciphertext, strlen(plaintext3), "915aa37c7d0d1b588748647fe0ebd1546cb8369e");
    check_hex(tag, 16, "4b13d6d5155c1d874fb5eb90581acc76");
}

static void test_tamper_rejected(void)
{
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    uint8_t nonce[16] = { 0 };
    const char *plaintext = "hello";
    uint8_t ciphertext[8];
    uint8_t tag[16];
    CHECK_INT_EQ(thalovant_aes_gcm_encrypt(key, nonce, 16, NULL, 0, (const uint8_t *)plaintext, 5,
                                           ciphertext, tag),
                 THALOVANT_OK);
    uint8_t recovered[8];
    tag[0] ^= 0x01;
    CHECK_INT_EQ(thalovant_aes_gcm_decrypt(key, nonce, 16, NULL, 0, ciphertext, 5, tag, recovered),
                 THALOVANT_ERR_AUTH);
    tag[0] ^= 0x01;
    ciphertext[0] ^= 0x01;
    CHECK_INT_EQ(thalovant_aes_gcm_decrypt(key, nonce, 16, NULL, 0, ciphertext, 5, tag, recovered),
                 THALOVANT_ERR_AUTH);
    ciphertext[0] ^= 0x01;
    CHECK_INT_EQ(thalovant_aes_gcm_decrypt(key, nonce, 16, NULL, 0, ciphertext, 5, tag, recovered),
                 THALOVANT_OK);
    CHECK(memcmp(recovered, plaintext, 5) == 0);
}

static void test_runtime_key(void)
{
    uint8_t key[16];
    CHECK_INT_EQ(thalovant_crypto_runtime_key("0123456789abcdefEXTRA-IGNORED", key),
                 THALOVANT_OK);
    CHECK(memcmp(key, "0123456789abcdef", 16) == 0);
    CHECK_INT_EQ(thalovant_crypto_runtime_key("  0123456789abcdef  ", key), THALOVANT_OK);
    CHECK(memcmp(key, "0123456789abcdef", 16) == 0);
    CHECK_INT_EQ(thalovant_crypto_runtime_key(NULL, key), THALOVANT_ERR_MISSING);
    CHECK_INT_EQ(thalovant_crypto_runtime_key("   ", key), THALOVANT_ERR_MISSING);
    CHECK_INT_EQ(thalovant_crypto_runtime_key("short", key), THALOVANT_ERR_INVALID);
}

void tlv_test_crypto(void)
{
    test_sha256();
    test_nist_vectors();
    test_node_interop_vectors();
    test_tamper_rejected();
    test_runtime_key();
}
