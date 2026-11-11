/* Tiny test helpers shared by THC's unit tests. */
#ifndef THC_TEST_UTIL_H
#define THC_TEST_UTIL_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do {                                                                       \
        g_checks++;                                                            \
        if (strcmp((a), (b)) != 0) {                                           \
            g_failures++;                                                      \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(fabs((double)(a) - (double)(b)) < 1e-6)

#define TEST_DONE()                                                            \
    do {                                                                       \
        printf("%d checks, %d failures\n", g_checks, g_failures);              \
        return g_failures ? 1 : 0;                                             \
    } while (0)

#endif
