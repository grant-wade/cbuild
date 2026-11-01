#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "math.h"

enum { SKIP_EXIT_CODE = 77, SLOW_RANGE = 2000 };

static int failures = 0;

#define EXPECT_EQ(actual, expected)                                              \
    do {                                                                         \
        int actual_value = (actual);                                             \
        int expected_value = (expected);                                         \
        if (actual_value != expected_value) {                                    \
            printf("%s:%d: %s is %d, expected %d\n", __FILE__, __LINE__, #actual, \
                   actual_value, expected_value);                                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_arithmetic(void) {
    EXPECT_EQ(add(2, 3), 5);
    EXPECT_EQ(add(-2, 2), 0);
    EXPECT_EQ(subtract(2, 3), -1);
    EXPECT_EQ(multiply(-4, 3), -12);
    EXPECT_EQ(multiply(7, 0), 0);
}

static void test_division(void) {
    EXPECT_EQ(divide(7, 2), 3);
    EXPECT_EQ(divide(-7, 2), -3);
    EXPECT_EQ(divide(7, 0), 0);
    EXPECT_EQ(modulus(7, 2), 1);
    EXPECT_EQ(modulus(7, 0), 0);
}

static void test_power(void) {
    EXPECT_EQ(power(2, 10), 1024);
    EXPECT_EQ(power(-3, 3), -27);
    EXPECT_EQ(power(5, 0), 1);
    EXPECT_EQ(power(0, 0), 1);
}

static void test_round_trips(void) {
    for (int a = -SLOW_RANGE; a <= SLOW_RANGE; a++) {
        for (int b = -SLOW_RANGE; b <= SLOW_RANGE; b++) {
            EXPECT_EQ(subtract(add(a, b), b), a);
            if (b != 0) {
                EXPECT_EQ(add(multiply(divide(a, b), b), modulus(a, b)), a);
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *suite = argc > 1 ? argv[1] : "";
    if (strcmp(suite, "arithmetic") == 0) {
        test_arithmetic();
    } else if (strcmp(suite, "division") == 0) {
        test_division();
    } else if (strcmp(suite, "power") == 0) {
        test_power();
    } else if (strcmp(suite, "round_trips") == 0) {
        if (!getenv("MATH_SLOW_TESTS")) {
            printf("set MATH_SLOW_TESTS to run this suite\n");
            return SKIP_EXIT_CODE;
        }
        test_round_trips();
    } else {
        printf("unknown suite '%s'\n", suite);
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
