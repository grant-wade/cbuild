#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (cbuild_run_command(ctx, cbuild_command(ctx, "prepare", "echo prepared")) != 0) return 1;
    cbuild_add_source(ctx, cbuild_static_library(ctx, "Setup"), "setup.c");
    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
