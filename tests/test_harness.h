#ifndef NYX_TEST_HARNESS_H
#define NYX_TEST_HARNESS_H

#include <stdio.h>

extern int nyx_test_failures;
extern int nyx_test_count;

#define TEST_CHECK(cond)                                                         \
    do {                                                                         \
        nyx_test_count++;                                                        \
        if (!(cond)) {                                                           \
            nyx_test_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                        \
    } while (0)

#define TEST_CHECK_EQ_INT(actual, expected)                                                    \
    do {                                                                                        \
        nyx_test_count++;                                                                       \
        long long _a = (long long)(actual);                                                     \
        long long _e = (long long)(expected);                                                   \
        if (_a != _e) {                                                                          \
            nyx_test_failures++;                                                                 \
            fprintf(stderr, "  FAIL %s:%d: expected %lld, got %lld\n", __FILE__, __LINE__, _e, _a); \
        }                                                                                        \
    } while (0)

#define TEST_CHECK_STREQ(actual, expected)                                                     \
    do {                                                                                        \
        nyx_test_count++;                                                                       \
        const char *_a = (actual);                                                               \
        const char *_e = (expected);                                                             \
        if (strcmp(_a, _e) != 0) {                                                                \
            nyx_test_failures++;                                                                  \
            fprintf(stderr, "  FAIL %s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, _e, _a); \
        }                                                                                        \
    } while (0)

#define TEST_SUITE(name) fprintf(stderr, "-- %s --\n", name)

#endif
