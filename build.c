#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/suite/scenarios.h"

#ifdef _WIN32
#define AMALGAMATE_PROGRAM "build/amalgamate.exe"
#define AMALGAMATE_COMMAND ".\\build\\amalgamate.exe"
#else
#define AMALGAMATE_PROGRAM "build/amalgamate"
#define AMALGAMATE_COMMAND "./build/amalgamate"
#endif

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))

enum { SCENARIO_TIMEOUT_SECONDS = 120, SCENARIO_SKIP_EXIT = 77 };

static const char *const suite_sources[] = {
    "tests/suite/api.c",     "tests/suite/example.c", "tests/suite/fake_compiler.c",
    "tests/suite/harness.c",
    "tests/suite/header.c",  "tests/suite/json.c",    "tests/suite/main.c",
    "tests/suite/project.c", "tests/suite/regress.c", "tests/suite/runner.c",
    "tests/suite/subproject.c",
};

/* The targets scenarios run from or depend on. */
typedef struct {
    target_t *suite;
    target_t *suite_split;
    target_t *regress;
    target_t *runner;
} suite_targets_t;

static target_t *add_suite(cbuild_context_t *ctx, const char *name) {
    target_t *suite = cbuild_executable(ctx, name);
    for (int i = 0; i < COUNT_OF(suite_sources); ++i) cbuild_add_source(ctx, suite, suite_sources[i]);
    return suite;
}

static target_t *add_fixture_program(cbuild_context_t *ctx, const char *name, const char *source) {
    target_t *program = cbuild_executable(ctx, name);
    cbuild_add_source(ctx, program, source);
    cbuild_add_include_dir(ctx, program, ".");
    return program;
}

static test_t *add_scenario_test(cbuild_context_t *ctx, const char *test_name, target_t *suite, const char *scenario,
                                 const char *label) {
    const char *args[] = { scenario, test_name };
    test_t *test = cbuild_add_test(ctx, test_name, suite, args, 2);
    cbuild_test_add_label(ctx, test, label);
    cbuild_test_set_timeout(ctx, test, SCENARIO_TIMEOUT_SECONDS);
    cbuild_test_set_skip_code(ctx, test, SCENARIO_SKIP_EXIT);
    return test;
}

static void add_scenario(cbuild_context_t *ctx, const suite_targets_t *targets, const char *group, const char *name) {
    char scenario[128];
    snprintf(scenario, sizeof(scenario), "%s.%s", group, name);
    test_t *test = add_scenario_test(ctx, scenario, targets->suite, scenario, group);
    if (strcmp(group, "regress") == 0) cbuild_test_add_dependency(ctx, test, targets->regress);
    if (strcmp(group, "runner") == 0) cbuild_test_add_dependency(ctx, test, targets->runner);
    if (strcmp(group, "api") == 0) {
        char split_name[128];
        snprintf(split_name, sizeof(split_name), "split.%s", name);
        add_scenario_test(ctx, split_name, targets->suite_split, scenario, "split");
    }
}

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;

    CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "cbuild.h", "tests/suite/scenarios.h");

    const char *compiler = getenv("CC");
#ifdef _WIN32
    cbuild_set_compiler(ctx, compiler && *compiler ? compiler : "cl");
#else
    cbuild_set_compiler(ctx, compiler && *compiler ? compiler : "cc");
#endif
    cbuild_set_output_dir(ctx, "build");

    config_t *config = cbuild_config_new(ctx, "Tests");
    cbuild_config_set_std(ctx, config, "c11");
    cbuild_config_set_warnings(ctx, config, 2);
#ifdef _WIN32
    cbuild_config_add_cflags(ctx, config, "/D_CRT_SECURE_NO_WARNINGS");
#else
    cbuild_config_add_cflags(ctx, config, "-Werror");
#endif
    cbuild_set_active_config(ctx, config);

    target_t *amalgamate = cbuild_executable(ctx, "amalgamate");
    cbuild_add_source(ctx, amalgamate, "tools/amalgamate.c");
    cbuild_register_subcommand(ctx, "amalgamate", amalgamate, AMALGAMATE_COMMAND, NULL, NULL);

    char *check_argv[] = { (char *)AMALGAMATE_PROGRAM, (char *)"--check" };
    target_t *check_header = cbuild_dummy_target(ctx, "check_header");
    cbuild_add_link_target(ctx, check_header, amalgamate);
    cbuild_target_add_post_command(ctx, check_header, cbuild_command_argv(ctx, "check cbuild.h", check_argv, 2));

    suite_targets_t targets;
    targets.suite = add_suite(ctx, "suite");
    cbuild_add_link_target(ctx, targets.suite, check_header);
    cbuild_add_source(ctx, targets.suite, "tests/suite/implementation.c");
    cbuild_add_include_dir(ctx, targets.suite, ".");

    targets.suite_split = add_suite(ctx, "suite_split");
    cbuild_add_link_target(ctx, targets.suite_split, check_header);
    cbuild_add_source(ctx, targets.suite_split, "src/*.c");
    cbuild_add_include_dir(ctx, targets.suite_split, "src");

    targets.regress = add_fixture_program(ctx, "regress_cbuild", "tests/fixtures/regress/build.c");
    targets.runner = add_fixture_program(ctx, "runner_cbuild", "tests/fixtures/runner/build.c");

    target_t *compiler_kinds = cbuild_executable(ctx, "compiler_kinds");
    cbuild_add_source(ctx, compiler_kinds, "tests/suite/compiler_kinds.c");
    cbuild_add_include_dir(ctx, compiler_kinds, ".");
    cbuild_add_link_target(ctx, compiler_kinds, check_header);
    cbuild_test_add_label(ctx, cbuild_add_test(ctx, "api.compiler_kinds", compiler_kinds, NULL, 0), "api");

#define X(group, name) add_scenario(ctx, &targets, #group, #name);
    SCENARIOS(X)
#undef X

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
