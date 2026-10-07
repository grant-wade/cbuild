#include "spaced.h"
#include "extra.h"

#ifndef TARGET_CC
#error "util.c must be compiled with the per-target compiler"
#endif

#ifndef EXTRA_VALUE
#define EXTRA_VALUE 3
#endif

int util_value(void) {
    return EXTRA_VALUE;
}
