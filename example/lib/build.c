#define CBUILD_IMPLEMENTATION
#include "../../cbuild.h"

#include <stdio.h>

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) {
        fprintf(stderr, "Failed to create cbuild context\n");
        return 1;
    }

    CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "../../cbuild.h");

    cbuild_set_output_dir(ctx, "build");

    target_t *math = cbuild_static_library(ctx, "math");
    cbuild_add_source(ctx, math, "src/*.c");
    cbuild_add_include_dir(ctx, math, "src");

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
