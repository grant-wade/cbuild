#define CBUILD_IMPLEMENTATION
#include "../cbuild.h"

#include <stdio.h>

#ifdef _WIN32
#define SUBPROJECT_BUILD_EXE "lib/cbuild.exe"
#define SUBPROJECT_EXE ".\\cbuild.exe"
#define BUILD_SUBPROJECT_COMMAND "cl /nologo /Fe:lib\\cbuild.exe lib\\build.c"
#define RUN_COMMAND ".\\build\\main.exe"
#else
#define SUBPROJECT_BUILD_EXE "lib/cbuild"
#define SUBPROJECT_EXE "./cbuild"
#define BUILD_SUBPROJECT_COMMAND "cc -o lib/cbuild lib/build.c"
#define RUN_COMMAND "./build/main"
#endif

static int init_dependency(cbuild_context_t *ctx) {
    if (!cbuild_file_exists(SUBPROJECT_BUILD_EXE) &&
        cbuild_file_exists("lib/build.c")) {
        command_t *cmd = cbuild_command(
            ctx, "bootstrap math subproject", BUILD_SUBPROJECT_COMMAND);
        if (!cmd || cbuild_run_command(ctx, cmd) != 0) {
            fprintf(stderr, "Failed to compile the math subproject build program\n");
            return -1;
        }
    }
    return 0;
}

static void add_tests(cbuild_context_t *ctx, target_t *app, target_t *math) {
    target_t *test_math = cbuild_executable(ctx, "test_math");
    cbuild_add_source(ctx, test_math, "tests/test_math.c");
    cbuild_add_include_dir(ctx, test_math, "lib/src");
    cbuild_add_link_target(ctx, test_math, math);

    const char *const suites[] = { "arithmetic", "division", "power" };
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        char name[32];
        snprintf(name, sizeof(name), "math_%s", suites[i]);
        test_t *unit = cbuild_add_test(ctx, name, test_math, &suites[i], 1);
        cbuild_test_add_label(ctx, unit, "unit");
    }

    const char *round_trips[] = { "round_trips" };
    test_t *slow = cbuild_add_test(ctx, "math_round_trips", test_math, round_trips, 1);
    cbuild_test_add_label(ctx, slow, "slow");
    cbuild_test_set_skip_code(ctx, slow, 77);
    cbuild_test_set_timeout(ctx, slow, 60);

    test_t *smoke = cbuild_add_test(ctx, "main_runs", app, NULL, 0);
    cbuild_test_add_label(ctx, smoke, "smoke");
}

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) {
        fprintf(stderr, "Failed to create cbuild context\n");
        return 1;
    }

    CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "../cbuild.h");

    cbuild_set_output_dir(ctx, "build");
    cbuild_enable_compile_commands(ctx, 1);

    if (init_dependency(ctx) != 0) {
        cbuild_context_free(ctx);
        return 1;
    }

    CBUILD_SUBPROJECT(ctx, math_project, "lib", SUBPROJECT_EXE);
    target_t *math = cbuild_subproject_get_target(ctx, math_project, "math");
    if (!math) {
        cbuild_context_free(ctx);
        return 1;
    }

    target_t *app = cbuild_executable(ctx, "main");
    cbuild_add_source(ctx, app, "main.c");
    cbuild_add_include_dir(ctx, app, "lib/src");
    cbuild_add_link_target(ctx, app, math);

    cbuild_register_subcommand(ctx, "run", app, RUN_COMMAND, NULL, NULL);
    add_tests(ctx, app, math);

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
