/* The whole test framework: CHECK counts failures, TEST_RESULT is main's return value. */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

static int test_failures;
static int test_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        test_checks++;                                                       \
        if (!(cond)) {                                                       \
            test_failures++;                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        long long test_a = (long long)(a), test_b = (long long)(b);                                 \
        test_checks++;                                                                              \
        if (test_a != test_b) {                                                                     \
            test_failures++;                                                                        \
            printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, test_a, test_b); \
        }                                                                                           \
    } while (0)

#define CHECK_STR(a, b)                                                                               \
    do {                                                                                              \
        const char *test_a = (a), *test_b = (b);                                                      \
        test_checks++;                                                                                \
        if (strcmp(test_a, test_b) != 0) {                                                            \
            test_failures++;                                                                          \
            printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, test_a, test_b); \
        }                                                                                             \
    } while (0)

#define TEST_RESULT() (printf("%d checks, %d failed\n", test_checks, test_failures), test_failures != 0)

#endif /* TEST_H */
