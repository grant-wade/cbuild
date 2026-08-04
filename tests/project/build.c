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

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
