#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#ifdef _WIN32
#define CHILD_EXE ".\\cbuild.exe"
#else
#define CHILD_EXE "./cbuild"
#endif

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    subproject_t *child = cbuild_add_subproject(ctx, "child", "child", CHILD_EXE);
    target_t *setup = cbuild_subproject_get_target(ctx, child, "Setup");
    if (!setup) return 1;
    target_t *app = cbuild_executable(ctx, "app");
    cbuild_add_source(ctx, app, "main.c");
    cbuild_add_link_target(ctx, app, setup);
    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
