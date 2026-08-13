/* Tiny assert harness — no framework dependency. */
#ifndef THALOVANT_TEST_HARNESS_H
#define THALOVANT_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>

extern int tlv_test_checks;
extern int tlv_test_failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        tlv_test_checks++;                                                              \
        if (!(cond)) {                                                                  \
            tlv_test_failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define CHECK_INT_EQ(actual, expected)                                                  \
    do {                                                                                \
        long tlv_a = (long)(actual);                                                    \
        long tlv_e = (long)(expected);                                                  \
        tlv_test_checks++;                                                              \
        if (tlv_a != tlv_e) {                                                           \
            tlv_test_failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__,          \
                    __LINE__, #actual, tlv_a, tlv_e);                                   \
        }                                                                               \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                                  \
    do {                                                                                \
        const char *tlv_a = (actual);                                                   \
        const char *tlv_e = (expected);                                                 \
        tlv_test_checks++;                                                              \
        if (tlv_a == NULL || strcmp(tlv_a, tlv_e) != 0) {                               \
            tlv_test_failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", \
                    __FILE__, __LINE__, #actual, tlv_a ? tlv_a : "(null)", tlv_e);      \
        }                                                                               \
    } while (0)

#endif /* THALOVANT_TEST_HARNESS_H */
