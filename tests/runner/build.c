#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define PROBE_PATH "build/probe.exe"
#define SPACED_PATH "space dir/spaced probe.exe"
#else
#define PROBE_PATH "build/probe"
#define SPACED_PATH "space dir/spaced probe"
#endif

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))
#define PROBE(name, label, ...)                                                     \
    add_probe_test(ctx, probe, name, label, (const char *[]){ __VA_ARGS__ },        \
                   COUNT_OF(((const char *[]){ __VA_ARGS__ })))

static test_t *add_probe_test(cbuild_context_t *ctx, target_t *probe, const char *name,
                              const char *label, const char *const *args, int argc) {
    test_t *test = cbuild_add_test(ctx, name, probe, args, argc);
    cbuild_test_add_label(ctx, test, label);
    return test;
}

static void write_marker(void *user_data) {
    FILE *file = fopen((const char *)user_data, "w");
    if (file) fclose(file);
}

static int print_flag(const char *value, void *user_data) {
    (void)value;
    puts((const char *)user_data);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;

    const char *compiler = getenv("CC");
    if (!compiler || !*compiler) compiler = "cc";
    cbuild_set_compiler(ctx, compiler);
    cbuild_set_output_dir(ctx, "build");

    config_t *base = cbuild_config_new(ctx, "Runner");
    cbuild_config_set_std(ctx, base, "c11");
    cbuild_config_set_warnings(ctx, base, 2);
    const char *config_dir = getenv("RUNNER_CONFIG_OUTDIR");
    if (config_dir && *config_dir) cbuild_config_set_output_dir(ctx, base, config_dir);
    cbuild_set_active_config(ctx, base);

    target_t *probe = cbuild_executable(ctx, "probe");
    cbuild_add_source(ctx, probe, "src/probe.c");

    cbuild_add_test(ctx, "sel_plain", probe, NULL, 0);
    test_t *both = PROBE("sel_alpha_one", "sel_a", "--exit", "0");
    cbuild_test_add_label(ctx, both, "sel_c");
    PROBE("sel_alpha_two", "sel_b", "--exit", "0");
    PROBE("sel_beta_one", "sel_b", "--exit", "0");

    PROBE("res_pass", "res_pass", "--out", "passing-output\n");
    cbuild_test_set_skip_code(ctx, PROBE("res_skip", "res_skip", "--out", "skipped-output\n", "--exit", "125"), 125);
    PROBE("res_fail", "res_fail", "--out", "failing-stdout\n", "--err", "failing-stderr-no-newline", "--exit", "3");
    PROBE("res_skip_code_unset", "res_fail_more", "--exit", "125");
    cbuild_test_set_skip_code(ctx, PROBE("res_skip_code_other", "res_fail_more", "--exit", "3"), 125);
    PROBE("res_terminated", "res_fail_more", "--terminate", "--exit", "0");
    const char *missing_program[] = { "no-such-program-for-cbuild-tests" };
    cbuild_test_add_label(ctx, cbuild_add_test(ctx, "res_cannot_start", NULL, missing_program, 1), "res_fail_more");

    PROBE("env_stdin", "env_stdin", "--stdin-empty");
    PROBE("env_var", "env_var", "--env", "RUNNER_VALUE");
    PROBE("env_args", "env_args", "--echo-args", "two words", "", "quote\"d", "back\\slash", "trailing space\\", "a*b");
    PROBE("weird name/with:chars", "env_name", "--out", "weird-output\n");

    target_t *spaced = cbuild_executable(ctx, "spaced");
    cbuild_add_source(ctx, spaced, "src/probe.c");
    cbuild_set_output_file(ctx, spaced, SPACED_PATH);
    const char *spaced_args[] = { "--out", "spaced-output\n" };
    cbuild_add_test(ctx, "env_spaced_program", spaced, spaced_args, 2);

    cbuild_test_set_timeout(ctx, PROBE("time_own", "time_own", "--out", "before-timeout\n", "--sleep", "60000"), 1);
    PROBE("time_default", "time_default", "--sleep", "60000");
    cbuild_test_set_timeout(ctx, PROBE("time_negative", "time_default", "--sleep", "60000"), -5);
    cbuild_test_set_timeout(ctx, PROBE("time_own_longer", "time_longer", "--sleep", "2000"), 60);

    PROBE("par_one", "par", "--rendezvous", "sync/par_one", "sync/par_two");
    PROBE("par_two", "par", "--rendezvous", "sync/par_two", "sync/par_one");
    for (int i = 0; i < 3; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "serial_%d", i);
        PROBE(name, "serial", "--exclusive", "sync/serial", "150");
    }
    for (int i = 0; i < 4; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "lock_same_%d", i);
        cbuild_test_set_lock(ctx, PROBE(name, "lock_same", "--exclusive", "sync/lock_same", "150"), "same");
    }
    cbuild_test_set_lock(ctx, PROBE("lock_x", "lock_distinct", "--rendezvous", "sync/lock_x", "sync/lock_y"), "x");
    cbuild_test_set_lock(ctx, PROBE("lock_y", "lock_distinct", "--rendezvous", "sync/lock_y", "sync/lock_x"), "y");
    test_t *cleared_null = PROBE("lock_cleared_null", "lock_cleared", "--rendezvous", "sync/null", "sync/empty");
    test_t *cleared_empty = PROBE("lock_cleared_empty", "lock_cleared", "--rendezvous", "sync/empty", "sync/null");
    cbuild_test_set_lock(ctx, cleared_null, "cleared");
    cbuild_test_set_lock(ctx, cleared_empty, "cleared");
    cbuild_test_set_lock(ctx, cleared_null, NULL);
    cbuild_test_set_lock(ctx, cleared_empty, "");
    cbuild_test_set_lock(ctx, PROBE("lock_holder", "lock_pass_over", "--rendezvous", "sync/holder", "sync/free"), "held");
    cbuild_test_set_lock(ctx, PROBE("lock_waiter", "lock_pass_over", "--exit", "0"), "held");
    PROBE("lock_free", "lock_pass_over", "--rendezvous", "sync/free", "sync/holder");

    target_t *marker = cbuild_dummy_target(ctx, "marker");
    cbuild_target_add_command(ctx, marker, cbuild_command_function(ctx, "write marker", write_marker, "dep-marker.txt"));
    cbuild_test_add_dependency(ctx, PROBE("dep_extra", "dep_extra", "--require", "dep-marker.txt"), marker);

    target_t *owner = cbuild_dummy_target(ctx, "owner");
    cbuild_target_add_command(ctx, owner, cbuild_command_function(ctx, "write owner marker", write_marker, "owner-marker.txt"));
    const char *owned_args[] = { PROBE_PATH, "--require", "owner-marker.txt" };
    test_t *owned = cbuild_add_test(ctx, "dep_program_with_target", owner, owned_args, 3);
    cbuild_test_add_dependency(ctx, owned, probe);

    target_t *versioned = cbuild_executable(ctx, "versioned");
    cbuild_add_source(ctx, versioned, "src/versioned.c");
    cbuild_add_test(ctx, "dep_versioned", versioned, NULL, 0);

    target_t *broken = cbuild_executable(ctx, "broken");
    cbuild_add_source(ctx, broken, "src/broken.c");
    cbuild_add_test(ctx, "broken_target", broken, NULL, 0);
    cbuild_test_add_dependency(ctx, PROBE("broken_dep", "broken_dep", "--exit", "0"), broken);

    cbuild_register_flag(ctx, "before-check", 0, 0, CBUILD_FLAG_BEFORE_BUILD, "Print before the build",
                         print_flag, "before-flag-ran");
    cbuild_register_flag(ctx, "after-check", 0, 0, CBUILD_FLAG_AFTER_BUILD, "Print after the build",
                         print_flag, "after-flag-ran");

    const char *late_dir = getenv("RUNNER_LATE_OUTDIR");
    if (late_dir && *late_dir) cbuild_set_output_dir(ctx, late_dir);

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
