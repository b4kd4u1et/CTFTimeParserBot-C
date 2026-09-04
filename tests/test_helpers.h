#ifndef CTF_TEST_HELPERS_H
#define CTF_TEST_HELPERS_H

#include <stdio.h>
#include <string.h>

/* Minimal single-file test harness: each test_*.c program includes this,
 * uses CHECK()/CHECK_STR_EQ() to record pass/fail, and ends with
 * TEST_SUMMARY() which returns 1 (failure) or 0 (success) from main(). */

static int g_test_failures = 0;
static int g_test_count = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        g_test_count++;                                                    \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            g_test_failures++;                                            \
        }                                                                  \
    } while (0)

#define CHECK_STR_EQ(a, b, msg)                                            \
    do {                                                                   \
        g_test_count++;                                                   \
        const char *_a = (a);                                             \
        const char *_b = (b);                                             \
        if (!_a || !_b || strcmp(_a, _b) != 0) {                          \
            fprintf(stderr, "FAIL: %s:%d: %s (got \"%s\", want \"%s\")\n", \
                    __FILE__, __LINE__, msg, _a ? _a : "(null)", _b ? _b : "(null)"); \
            g_test_failures++;                                            \
        }                                                                 \
    } while (0)

#define TEST_SUMMARY()                                                     \
    do {                                                                   \
        if (g_test_failures > 0) {                                        \
            fprintf(stderr, "%d/%d checks FAILED\n", g_test_failures, g_test_count); \
            return 1;                                                     \
        }                                                                  \
        printf("%d/%d checks passed\n", g_test_count, g_test_count);      \
        return 0;                                                         \
    } while (0)

#endif
