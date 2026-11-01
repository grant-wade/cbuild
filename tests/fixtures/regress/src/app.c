#include <stdio.h>

#include "generated.h"
#include "spaced.h"

#ifndef MODE
#define MODE 0
#endif

int main(void) {
    printf("generated=%d util=%d mode=%d\n", GENERATED_VALUE, util_value(), MODE);
    return 0;
}
