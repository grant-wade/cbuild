#include "generated.h"
#include "math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FEATURE_VALUE
#define FEATURE_VALUE 0
#endif

int main(int argc, char **argv) {
    int sum = test_add(2, 3);
    int product = test_multiply(3, 4);

    printf("sum=%d product=%d generated=%d feature=%d\n",
           sum, product, GENERATED_VALUE, FEATURE_VALUE);

    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        return (sum == 5 && product == 12 && GENERATED_VALUE == 7) ? 0 : 1;
    }
    if (argc > 2 && strcmp(argv[1], "--exit") == 0) {
        return atoi(argv[2]);
    }
    if (argc > 2 && strcmp(argv[1], "--spin") == 0) {
        time_t end = time(NULL) + atoi(argv[2]);
        while (time(NULL) < end) {
        }
        return 0;
    }
    return 0;
}
