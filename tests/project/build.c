#define CBUILD_IMPLEMENTATION
#include "../../cbuild.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#define TEST_APP_PATH "build/test_app.exe"
#define TEST_RUN_COMMAND ".\\build\\test_app.exe"
#else
#define TEST_APP_PATH "build/test_app"
#define TEST_RUN_COMMAND "./build/test_app"
#endif

typedef struct {
    cbuild_context_t *ctx;
    target_t *target;
} feature_binding_t;

typedef struct {
    int prepared;
} command_state_t;

static int ensure_build_directory(void) {
    if (cbuild_dir_exists("build")) return 0;
#ifdef _WIN32
    return _mkdir("build");
#else
    return mkdir("build", 0777);
#endif
}

static void prepare_command(void *user_data) {
    command_state_t *state = (command_state_t *)user_data;
    if (ensure_build_directory() != 0 && errno != EEXIST) return;

    FILE *file = fopen("build/command-dependency.txt", "w");
    if (!file) return;
    fputs("prepared\n", file);
    fclose(file);
    state->prepared = 1;
}

static void dependent_command(void *user_data) {
    command_state_t *state = (command_state_t *)user_data;
    if (!state->prepared) return;

    FILE *file = fopen("build/command-main.txt", "w");
    if (!file) return;
    fputs("dependency-ran-first\n", file);
    fclose(file);
}

static void generate_header(void *user_data) {
    (void)user_data;
    if (ensure_build_directory() != 0 && errno != EEXIST) return;

    FILE *file = fopen("build/generated.h", "w");
    if (!file) return;
    fputs("#ifndef TEST_GENERATED_H\n", file);
    fputs("#define TEST_GENERATED_H\n", file);
    fputs("#define GENERATED_VALUE 7\n", file);
    fputs("#endif\n", file);
    fclose(file);
}

/* Exercises cbuild_spawn: run the built app and capture what it prints. */
static int spawn_check(const char *value, void *user_data) {
    (void)value;
    cbuild_context_t *ctx = (cbuild_context_t *)user_data;
    const char *args[] = { TEST_APP_PATH, "--exit", "4" };
    char *output = NULL;
    int rc = cbuild_spawn(ctx, args, 3, &output);
    printf("spawn rc=%d output=%s", rc, output ? output : "(none)\n");
    free(output);
    return rc == 4 ? 0 : 1;
}

static int enable_feature(const char *value, void *user_data) {
    (void)value;
    feature_binding_t *binding = (feature_binding_t *)user_data;
    return cbuild_add_define(binding->ctx, binding->target,
                             "FEATURE_VALUE=1") == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;

    CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "../../cbuild.h");

    const char *compiler = getenv("CC");
    if (compiler && *compiler) cbuild_set_compiler(ctx, compiler);

    cbuild_set_output_dir(ctx, "build");
    cbuild_enable_compile_commands(ctx, 1);

    config_t *base = cbuild_config_new(ctx, "CI");
    cbuild_config_set_std(ctx, base, "c11");
    cbuild_config_set_warnings(ctx, base, 2);
    cbuild_set_active_config(ctx, base);

    target_t *generated = cbuild_file_dep_target(
        ctx, "generated_header", "build/generated.h");
    cbuild_add_source(ctx, generated, "inputs/schema.txt");
    command_t *generate = cbuild_command_function(
        ctx, "generate header", generate_header, NULL);
    cbuild_target_add_command(ctx, generated, generate);

    target_t *math = cbuild_static_library(ctx, "math");
    cbuild_add_source(ctx, math, "src/math*.c");
    cbuild_add_include_dir(ctx, math, "src");
    cbuild_add_define_val(ctx, math, "MATH_BUILD", "1");

    config_t *shared_config = cbuild_config_new(ctx, "Shared");
    cbuild_config_set_std(ctx, shared_config, "c11");
    cbuild_config_set_warnings(ctx, shared_config, 2);
    cbuild_config_set_pic(ctx, shared_config, 1);

    target_t *shared = cbuild_shared_library(ctx, "shared");
    cbuild_target_set_config(ctx, shared, shared_config);
    cbuild_add_source(ctx, shared, "src/shared.c");
    cbuild_export_symbols(ctx, shared);

    target_t *app = cbuild_executable(ctx, "test_app");
    cbuild_add_source(ctx, app, "src/app.c");
    CBUILD_INCLUDES(ctx, app, "src", "build");
    cbuild_add_link_target(ctx, app, generated);
    cbuild_add_link_target(ctx, app, math);

    char *self_test_argv[] = { TEST_APP_PATH, "--self-test" };
    command_t *self_test = cbuild_command_argv(
        ctx, "run executable self-test", self_test_argv, 2);
    cbuild_target_add_post_command(ctx, app, self_test);

    command_state_t command_state = { 0 };
    command_t *prepare = cbuild_command_function(
        ctx, "prepare command", prepare_command, &command_state);
    command_t *dependent = cbuild_command_function(
        ctx, "dependent command", dependent_command, &command_state);
    cbuild_command_add_dependency(ctx, dependent, prepare);

    target_t *all = cbuild_dummy_target(ctx, "all");
    cbuild_add_link_target(ctx, all, app);
    cbuild_add_link_target(ctx, all, shared);
    cbuild_target_add_command(ctx, all, dependent);

    feature_binding_t feature_binding = { ctx, app };
    cbuild_register_flag(ctx, "feature", 'f', 0, CBUILD_FLAG_PRE,
                         "Build test_app with FEATURE_VALUE=1",
                         enable_feature, &feature_binding);
    cbuild_register_subcommand(ctx, "app", app, TEST_RUN_COMMAND, NULL, NULL);
    cbuild_register_flag(ctx, "spawn-check", 0, 0, CBUILD_FLAG_AFTER_BUILD,
                         "Run the built app through cbuild_spawn", spawn_check, ctx);

    const char *self_test_args[] = { "--self-test" };
    test_t *passing = cbuild_add_test(ctx, "app_self_test", app, self_test_args, 1);
    cbuild_test_add_label(ctx, passing, "unit");
    cbuild_test_add_dependency(ctx, passing, shared);
    cbuild_test_set_lock(ctx, passing, "app");

    const char *skip_args[] = { "--exit", "125" };
    test_t *skipped = cbuild_add_test(ctx, "app_skip", app, skip_args, 2);
    cbuild_test_add_label(ctx, skipped, "unit");
    cbuild_test_set_skip_code(ctx, skipped, 125);
    cbuild_test_set_lock(ctx, skipped, "app");

    const char *by_path_args[] = { TEST_APP_PATH, "--exit", "0" };
    test_t *by_path = cbuild_add_test(ctx, "app_by_path", NULL, by_path_args, 3);
    cbuild_test_add_dependency(ctx, by_path, app);

    /* Failing tests exist only on request, so the default --test run passes.
     * Reading the exit status before the graph exists exercises cbuild_get_flag_value. */
    int failing_tests = 0;
    cbuild_register_flag_bool(ctx, "failing-tests", 0, CBUILD_FLAG_PRE,
                              "Also register tests that fail", &failing_tests);
    const char *fail_code = NULL;
    cbuild_register_flag_str(ctx, "fail-code", 0, CBUILD_FLAG_PRE,
                             "Exit status of the failing test", &fail_code);
    if (cbuild_has_flag(argc, argv, "failing-tests", 0)) {
        const char *code = cbuild_get_flag_value(argc, argv, "fail-code", 0);
        const char *fail_args[] = { "--exit", code ? code : "1" };
        test_t *failing = cbuild_add_test(ctx, "app_fails", app, fail_args, 2);
        cbuild_test_add_label(ctx, failing, "failing");
        cbuild_test_set_skip_code(ctx, failing, 125);

        const char *spin_args[] = { "--spin", "30" };
        test_t *slow = cbuild_add_test(ctx, "app_timeout", app, spin_args, 2);
        cbuild_test_add_label(ctx, slow, "failing");
        cbuild_test_set_timeout(ctx, slow, 1);
    }

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
