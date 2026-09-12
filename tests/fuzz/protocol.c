/* Network-free libFuzzer entry point; no fuzzer dependency in the library. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "thalovant/thalovant.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    thalovant_json_tok tokens[128];
    const char *json = (const char *)data;
    char text[1024];
    uint8_t bytes[1024];
    size_t cap = size == 0 ? 0 : data[0];
    int count = thalovant_json_parse(json, size, tokens, 128);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            long number;
            (void)thalovant_json_as_string(json, &tokens[i], text, cap);
            (void)thalovant_json_as_int(json, &tokens[i], &number);
            (void)thalovant_json_as_bool(json, &tokens[i], false);
            (void)thalovant_json_is_truthy(json, &tokens[i]);
        }
    }
    thalovant_json_tok root;
    if (thalovant_json_scan(json, size, 0, &root) >= 0) {
        if (root.type == THALOVANT_JSON_ARRAY) {
            size_t cursor = 0;
            thalovant_json_tok elem;
            while (thalovant_json_scan_next(json, &root, &cursor, &elem) == 1) {
                (void)thalovant_json_as_string(json, &elem, text, cap);
            }
        } else if (root.type == THALOVANT_JSON_OBJECT) {
            thalovant_json_tok value;
            (void)thalovant_json_scan_key(json, &root, "data", &value);
        }
    }
    thalovant_identity identity;
    thalovant_wire_frame frame;
    thalovant_ask_event ask;
    thalovant_intent_event intents;
    (void)thalovant_identity_parse(json, size, &identity);
    (void)thalovant_wire_parse(json, size, &frame);
    (void)thalovant_ask_classify(json, size, "fuzz-request", &ask);
    (void)thalovant_ask_audio_decode(json, size, NULL, text, sizeof(text), bytes, cap);
    (void)thalovant_ask_event_language(json, size, NULL, text, cap);
    char pattern[1024];
    size_t pattern_len = size < sizeof(pattern) - 1 ? size : sizeof(pattern) - 1;
    memcpy(pattern, data, pattern_len);
    pattern[pattern_len] = '\0';
    (void)thalovant_speakable(pattern, NULL, 0, text, cap);
    thalovant_location location = {pattern, NULL, NULL, NULL, false, 0, 0};
    (void)thalovant_build_location(&location, text, cap);
    (void)thalovant_intent_classify(json, size, "fuzz-request", &intents);
    (void)thalovant_hex_decode(json, size, bytes, cap);
    (void)thalovant_base64_decode(json, size, bytes, cap);

    /* Codec round trips vary both content and short output buffers. */
    size_t len = size < sizeof(bytes) / 2 ? size : sizeof(bytes) / 2;
    int encoded = thalovant_base64_encode(data, len, text, sizeof(text));
    assert(encoded >= 0);
    int decoded = thalovant_base64_decode(text, (size_t)encoded, bytes, sizeof(bytes));
    assert(decoded == (int)len && memcmp(data, bytes, len) == 0);
    (void)thalovant_base64_encode(data, len, text, cap);
    (void)thalovant_hex_encode(data, len, text, cap);
    return 0;
}
