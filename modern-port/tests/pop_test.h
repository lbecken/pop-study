/* pop_test.h - a very small test harness.  New code. */
#ifndef POP_TEST_H
#define POP_TEST_H

#include <stdio.h>

static int pop_test_failures = 0;
static int pop_test_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        pop_test_checks++;                                                   \
        if (!(cond)) {                                                       \
            pop_test_failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long pop_a_ = (long)(actual), pop_e_ = (long)(expected);             \
        pop_test_checks++;                                                   \
        if (pop_a_ != pop_e_) {                                              \
            pop_test_failures++;                                             \
            printf("FAIL %s:%d: %s = %ld (0x%lX), expected %ld (0x%lX)\n",   \
                   __FILE__, __LINE__, #actual, pop_a_, (unsigned long)pop_a_, \
                   pop_e_, (unsigned long)pop_e_);                           \
        }                                                                    \
    } while (0)

#define RUN(test)                                                            \
    do {                                                                     \
        int pop_before_ = pop_test_failures;                                 \
        test();                                                              \
        printf("%s %s\n", pop_test_failures == pop_before_ ? "ok  " : "FAIL", #test); \
    } while (0)

/* Exit status 77 tells CTest the test was skipped (SKIP_RETURN_CODE). */
#define POP_TEST_SKIP 77

static int pop_test_summary(void)
{
    printf("%d checks, %d failures\n", pop_test_checks, pop_test_failures);
    return pop_test_failures ? 1 : 0;
}

#endif /* POP_TEST_H */
