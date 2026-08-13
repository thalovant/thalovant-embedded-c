/*
 * Frame fixtures in this file were captured byte-for-byte from the Node
 * SDK (JSON.stringify of transport-core's hello message and
 * encodeHiveBinaryFrame from dist/src/wire.js).
 */
#include <stdint.h>

#include "harness.h"
#include "thalovant/codec.h"
#include "thalovant/wire.h"

static const char HELLO_FIXTURE[] =
    "{\"msg_type\":\"hello\",\"payload\":{\"pubkey\":\"\",\"session\":{\"session_id\":"
    "\"thalovant-node-abc\"},\"site_id\":\"kitchen\"},\"metadata\":{},\"route\":[],"
    "\"node\":null,\"target_site_id\":null,\"target_pubkey\":null,\"source_peer\":null}";

static void test_serialize_matches_node(void)
{
    char out[512];
    int len = thalovant_wire_hello(NULL, "thalovant-node-abc", "kitchen", out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, HELLO_FIXTURE);

    thalovant_hive_message msg = { "bus", "{\"type\":\"speak\"}", NULL, NULL,
                                   NULL,  NULL,                NULL, NULL };
    len = thalovant_wire_serialize(&msg, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\"},\"metadata\":{},"
                      "\"route\":[],\"node\":null,\"target_site_id\":null,"
                      "\"target_pubkey\":null,\"source_peer\":null}");

    thalovant_hive_message routed = { "query", "{}",     "{\"query_id\":\"q\"}", NULL,
                                      "node1", "site-2", NULL,                   NULL };
    len = thalovant_wire_serialize(&routed, out, sizeof(out));
    CHECK(len > 0);
    CHECK_STR_EQ(out, "{\"msg_type\":\"query\",\"payload\":{},\"metadata\":{\"query_id\":\"q\"},"
                      "\"route\":[],\"node\":\"node1\",\"target_site_id\":\"site-2\","
                      "\"target_pubkey\":null,\"source_peer\":null}");
}

static void test_parse_round_trip(void)
{
    thalovant_wire_frame frame;
    CHECK_INT_EQ(thalovant_wire_parse(HELLO_FIXTURE, sizeof(HELLO_FIXTURE) - 1, &frame),
                 THALOVANT_OK);
    CHECK_STR_EQ(frame.msg_type, "hello");
    CHECK(frame.payload != NULL);
    CHECK(!frame.has_node);
    CHECK(!frame.has_target_site_id);
    /* payload slice is the raw JSON object */
    CHECK(frame.payload[0] == '{' && frame.payload[frame.payload_len - 1] == '}');
    char payload[256];
    CHECK(frame.payload_len < sizeof(payload));
    memcpy(payload, frame.payload, frame.payload_len);
    payload[frame.payload_len] = '\0';
    CHECK_STR_EQ(payload, "{\"pubkey\":\"\",\"session\":{\"session_id\":\"thalovant-node-abc\"},"
                          "\"site_id\":\"kitchen\"}");
    /* metadata slice */
    CHECK(frame.metadata != NULL);
    CHECK_INT_EQ((int)frame.metadata_len, 2);
}

static void test_handshake_detection(void)
{
    const char *handshake = "{\"msg_type\":\"handshake\",\"payload\":{\"preshared_key\":true}}";
    thalovant_wire_frame frame;
    CHECK_INT_EQ(thalovant_wire_parse(handshake, strlen(handshake), &frame), THALOVANT_OK);
    CHECK(thalovant_wire_is_preshared_handshake(&frame));

    const char *shake = "{\"msg_type\":\"shake\",\"payload\":{\"preshared_key\":1}}";
    CHECK_INT_EQ(thalovant_wire_parse(shake, strlen(shake), &frame), THALOVANT_OK);
    CHECK(thalovant_wire_is_preshared_handshake(&frame));

    /* An envelope/handshake payload means an unsupported handshake mode. */
    const char *pubkey_mode =
        "{\"msg_type\":\"handshake\",\"payload\":{\"preshared_key\":true,\"envelope\":\"x\"}}";
    CHECK_INT_EQ(thalovant_wire_parse(pubkey_mode, strlen(pubkey_mode), &frame), THALOVANT_OK);
    CHECK(!thalovant_wire_is_preshared_handshake(&frame));

    const char *no_key = "{\"msg_type\":\"handshake\",\"payload\":{\"preshared_key\":false}}";
    CHECK_INT_EQ(thalovant_wire_parse(no_key, strlen(no_key), &frame), THALOVANT_OK);
    CHECK(!thalovant_wire_is_preshared_handshake(&frame));

    const char *bus = "{\"msg_type\":\"bus\",\"payload\":{\"preshared_key\":true}}";
    CHECK_INT_EQ(thalovant_wire_parse(bus, strlen(bus), &frame), THALOVANT_OK);
    CHECK(!thalovant_wire_is_preshared_handshake(&frame));
}

static void test_json_envelope_round_trip(void)
{
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    uint8_t nonce[16];
    CHECK(thalovant_hex_decode("000102030405060708090a0b0c0d0e0f", 32, nonce, sizeof(nonce)) ==
          16);
    /* Node KAT: envelope fields are deterministic for this key/nonce. */
    uint8_t plaintext[] = "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\"}}";
    size_t plaintext_len = sizeof(plaintext) - 1;
    char envelope[512];
    int len = thalovant_envelope_encrypt_json(key, nonce, plaintext, plaintext_len, envelope,
                                              sizeof(envelope));
    CHECK(len > 0);
    CHECK_STR_EQ(envelope,
                 "{\"ciphertext\":\"eecae278f07b4787d3b62d79b68abd4a8ff45bb2414001531bef052e"
                 "4bc58e1878e780fe92d226f6597a3fda42\",\"tag\":"
                 "\"9df782125f2077b8d91c96d9efecaff0\",\"nonce\":"
                 "\"000102030405060708090a0b0c0d0e0f\"}");
    CHECK(thalovant_wire_is_encrypted(envelope, (size_t)len));
    CHECK(!thalovant_wire_is_encrypted(HELLO_FIXTURE, sizeof(HELLO_FIXTURE) - 1));

    uint8_t recovered[256];
    size_t recovered_len = 0;
    CHECK_INT_EQ(thalovant_envelope_decrypt_json(key, envelope, (size_t)len, recovered,
                                                 sizeof(recovered), &recovered_len),
                 THALOVANT_OK);
    CHECK_INT_EQ((int)recovered_len, (int)plaintext_len);
    CHECK_STR_EQ((const char *)recovered, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\"}}");
}

static void test_json_envelope_base64_autodetect(void)
{
    /* Same sealed bytes, base64-encoded fields: the nonce is not 16/12
     * bytes of hex, so the Node auto-detection selects Base64. */
    const char *envelope =
        "{\"ciphertext\":\"7sriePB7R4fTti15toq9So/0W7JBQAFTG+8FLkvFjhh454D+ktIm9ll6P9pC\","
        "\"tag\":\"nfeCEl8gd7jZHJbZ7+yv8A==\",\"nonce\":\"AAECAwQFBgcICQoLDA0ODw==\"}";
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    uint8_t recovered[256];
    size_t recovered_len = 0;
    CHECK_INT_EQ(thalovant_envelope_decrypt_json(key, envelope, strlen(envelope), recovered,
                                                 sizeof(recovered), &recovered_len),
                 THALOVANT_OK);
    CHECK_STR_EQ((const char *)recovered, "{\"msg_type\":\"bus\",\"payload\":{\"type\":\"speak\"}}");
}

static void test_json_envelope_bad_key(void)
{
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    const uint8_t *wrong = (const uint8_t *)"fedcba9876543210";
    uint8_t nonce[16] = { 0 };
    uint8_t plaintext[] = "secret";
    char envelope[256];
    int len = thalovant_envelope_encrypt_json(key, nonce, plaintext, 6, envelope,
                                              sizeof(envelope));
    CHECK(len > 0);
    uint8_t recovered[64];
    size_t recovered_len = 0;
    CHECK_INT_EQ(thalovant_envelope_decrypt_json(wrong, envelope, (size_t)len, recovered,
                                                 sizeof(recovered), &recovered_len),
                 THALOVANT_ERR_AUTH);
}

static void test_binary_envelope_round_trip(void)
{
    const uint8_t *key = (const uint8_t *)"0123456789abcdef";
    uint8_t nonce[16];
    CHECK(thalovant_hex_decode("000102030405060708090a0b0c0d0e0f", 32, nonce, sizeof(nonce)) ==
          16);
    const uint8_t plaintext[] = "binary frame payload";
    uint8_t sealed[128];
    size_t sealed_len = 0;
    CHECK_INT_EQ(thalovant_envelope_encrypt_binary(key, nonce, plaintext, sizeof(plaintext) - 1,
                                                   sealed, sizeof(sealed), &sealed_len),
                 THALOVANT_OK);
    CHECK_INT_EQ((int)sealed_len, 16 + 20 + 16);
    CHECK(memcmp(sealed, nonce, 16) == 0); /* nonce prefix */
    uint8_t recovered[64];
    size_t recovered_len = 0;
    CHECK_INT_EQ(thalovant_envelope_decrypt_binary(key, sealed, sealed_len, recovered,
                                                   sizeof(recovered), &recovered_len),
                 THALOVANT_OK);
    CHECK_INT_EQ((int)recovered_len, 20);
    CHECK(memcmp(recovered, plaintext, 20) == 0);
    /* Too-short payloads are rejected like the Node SDK. */
    CHECK_INT_EQ(thalovant_envelope_decrypt_binary(key, sealed, 32, recovered, sizeof(recovered),
                                                   &recovered_len),
                 THALOVANT_ERR_INVALID);
}

static void test_authorization(void)
{
    /* base64("ThalovantNodeSDK/0.2.24:tlv-key-123") */
    char authorization[128];
    int len = thalovant_wire_authorization("ThalovantNodeSDK/0.2.24", "tlv-key-123",
                                           authorization, sizeof(authorization));
    CHECK(len > 0);
    CHECK_STR_EQ(authorization, "VGhhbG92YW50Tm9kZVNESy8wLjIuMjQ6dGx2LWtleS0xMjM=");
    char url[256];
    len = thalovant_wire_ws_url("wss://hub.example.com/ws", authorization, url, sizeof(url));
    CHECK(len > 0);
    CHECK_STR_EQ(url, "wss://hub.example.com/ws?authorization="
                      "VGhhbG92YW50Tm9kZVNESy8wLjIuMjQ6dGx2LWtleS0xMjM%3D");
    len = thalovant_wire_ws_url("wss://hub.example.com/ws?a=1", "abc", url, sizeof(url));
    CHECK(len > 0);
    CHECK_STR_EQ(url, "wss://hub.example.com/ws?a=1&authorization=abc");
}

static void test_binary_frame(void)
{
    /* Fixture: encodeHiveBinaryFrame(hello) from the Node SDK. */
    const char *expected_hex =
        "8c027b7d7b227075626b6579223a22222c2273657373696f6e223a7b2273657373696f6e5f6964223a2274"
        "68616c6f76616e742d6e6f64652d616263227d2c22736974655f6964223a226b69746368656e227d";
    thalovant_hive_message msg = {
        "hello",
        "{\"pubkey\":\"\",\"session\":{\"session_id\":\"thalovant-node-abc\"},"
        "\"site_id\":\"kitchen\"}",
        NULL, NULL, NULL, NULL, NULL, NULL,
    };
    uint8_t frame[256];
    size_t frame_len = 0;
    CHECK_INT_EQ(thalovant_wire_encode_binary(&msg, frame, sizeof(frame), &frame_len),
                 THALOVANT_OK);
    char hex[512];
    CHECK(thalovant_hex_encode(frame, frame_len, hex, sizeof(hex)) > 0);
    CHECK_STR_EQ(hex, expected_hex);

    char metadata[64];
    char payload[256];
    thalovant_wire_binary_frame decoded;
    CHECK_INT_EQ(thalovant_wire_decode_binary(frame, frame_len, metadata, sizeof(metadata),
                                              payload, sizeof(payload), &decoded),
                 THALOVANT_OK);
    CHECK_STR_EQ(decoded.msg_type, "hello");
    CHECK_INT_EQ(decoded.type_id, 6);
    CHECK_STR_EQ(metadata, "{}");
    CHECK_STR_EQ(payload, "{\"pubkey\":\"\",\"session\":{\"session_id\":\"thalovant-node-abc\"},"
                          "\"site_id\":\"kitchen\"}");

    /* Compressed frames are refused, not mis-decoded. */
    uint8_t compressed[4] = { 0x8d, 0x00, 0x00, 0x00 }; /* hello + compressed bit */
    CHECK_INT_EQ(thalovant_wire_decode_binary(compressed, sizeof(compressed), metadata,
                                              sizeof(metadata), payload, sizeof(payload),
                                              &decoded),
                 THALOVANT_ERR_UNSUPPORTED);

    CHECK_INT_EQ(thalovant_wire_msg_type_id("bus"), 1);
    CHECK_INT_EQ(thalovant_wire_msg_type_id("handshake"), 0);
    CHECK_INT_EQ(thalovant_wire_msg_type_id("unknown-type"), -1);
    CHECK_STR_EQ(thalovant_wire_msg_type_name(12), "bin");
}

void tlv_test_wire(void)
{
    test_serialize_matches_node();
    test_parse_round_trip();
    test_handshake_detection();
    test_json_envelope_round_trip();
    test_json_envelope_base64_autodetect();
    test_json_envelope_bad_key();
    test_binary_envelope_round_trip();
    test_authorization();
    test_binary_frame();
}
