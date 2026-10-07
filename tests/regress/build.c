/* Regression project for rebuild correctness and clean safety. Driven by
 * tests/run-regress.py, which builds a scratch copy and mutates it. */
#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#include <stdio.h>
#include <stdlib.h>

static void noop_command(void *user_data) {
    (void)user_data;
}

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;

    const char *compiler = getenv("CC");
    if (!compiler || !*compiler) compiler = "cc";
    cbuild_set_compiler(ctx, compiler);

    const char *out_dir = getenv("REGRESS_OUTDIR");
    if (!out_dir || !*out_dir) out_dir = "build";
    cbuild_set_output_dir(ctx, out_dir);

    static char generated_path[512];
    static char generator_path[512];
    snprintf(generated_path, sizeof(generated_path), "%s/generated.h", out_dir);
#ifdef _WIN32
    snprintf(generator_path, sizeof(generator_path), "%s/gen.exe", out_dir);
#else
    snprintf(generator_path, sizeof(generator_path), "%s/gen", out_dir);
#endif

    config_t *base = cbuild_config_new(ctx, "Regress");
    cbuild_config_set_std(ctx, base, "c11");
    cbuild_config_set_warnings(ctx, base, 2);
    cbuild_set_active_config(ctx, base);

    /* Declared before the generator it depends on, so only the dependency
     * graph (not declaration order) can get the generator built first. */
    target_t *generated = cbuild_file_dep_target(ctx, "generated", generated_path);
    char *generate_argv[] = { generator_path, generated_path };
    command_t *generate = cbuild_command_argv(ctx, "generate header", generate_argv, 2);
    cbuild_target_add_command(ctx, generated, generate);

    target_t *gen = cbuild_executable(ctx, "gen");
    cbuild_add_source(ctx, gen, "src/gen.c");
    cbuild_add_link_target(ctx, generated, gen);

    /* A file cbuild depends on but does not create: --clean must leave it. */
    target_t *vendored = cbuild_file_dep_target(ctx, "vendored", "vendor/data.txt");

    /* Per-target compiler: the extra define only reaches util.c this way. */
    static char util_compiler[512];
    snprintf(util_compiler, sizeof(util_compiler), "%s -DTARGET_CC=1", compiler);
    config_t *util_config = cbuild_config_new(ctx, "Util");
    cbuild_config_set_std(ctx, util_config, "c11");
    cbuild_config_set_warnings(ctx, util_config, 2);
    cbuild_config_set_compiler(ctx, util_config, util_compiler);

    target_t *util = cbuild_static_library(ctx, "util");
    cbuild_target_set_config(ctx, util, util_config);
    cbuild_add_source(ctx, util, "space dir/src/util.c");
    cbuild_add_include_dir(ctx, util, "space dir/include");
    const char *util_cflags = getenv("REGRESS_UTIL_CFLAGS");
    if (util_cflags && *util_cflags) cbuild_add_cflags(ctx, util, util_cflags);

    target_t *app = cbuild_executable(ctx, "regress_app");
    cbuild_add_source(ctx, app, "src/app.c");
    cbuild_add_include_dir(ctx, app, "space dir/include");
    cbuild_add_include_dir(ctx, app, out_dir);
    cbuild_add_link_target(ctx, app, generated);
    cbuild_add_link_target(ctx, app, vendored);
    cbuild_add_link_target(ctx, app, util);
#ifdef _WIN32
    cbuild_set_output_file(ctx, app, "dist/regress_app.exe");
#else
    cbuild_set_output_file(ctx, app, "dist/regress_app");
#endif
    const char *app_define = getenv("REGRESS_APP_DEFINE");
    if (app_define && *app_define) cbuild_add_define(ctx, app, app_define);

    if (getenv("REGRESS_COMMAND_CYCLE")) {
        command_t *first = cbuild_command_function(ctx, "cycle first", noop_command, NULL);
        command_t *second = cbuild_command_function(ctx, "cycle second", noop_command, NULL);
        cbuild_command_add_dependency(ctx, first, second);
        cbuild_command_add_dependency(ctx, second, first);
        target_t *cycle = cbuild_dummy_target(ctx, "cycle");
        cbuild_target_add_command(ctx, cycle, first);
    }

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
