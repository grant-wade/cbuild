#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

/* Longer than the time a surviving grandchild takes to create its file. */
enum { HANG_LIMIT_SECONDS = 60, SURVIVOR_WAIT_MS = 4000 };

void runner_setup(void) {
    copy_tree(root_path("tests/fixtures/runner"), ".");
    make_dir("sync");
    use_program(prebuilt_program("runner_cbuild"));
}

static const char *read_test_log(const char *name) {
    return read_text(format_text("build/tests/%s.log", name));
}

static const char *run_tests(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)(argc + 1));
    argv[0] = "--test";
    for (int i = 0; i < argc; ++i) argv[i + 1] = args[i];
    return h_build(argc + 1, argv);
}

static const char *run_failing_tests(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)(argc + 1));
    argv[0] = "--test";
    for (int i = 0; i < argc; ++i) argv[i + 1] = args[i];
    outcome_t outcome = h_cbuild(argc + 1, argv);
    if (outcome.status != 1) h_fail("--test exited with status %d, expected 1", outcome.status);
    return outcome.log;
}

#define RUN_TESTS(...) H_AT(run_tests(H_ARGS(__VA_ARGS__)))
#define RUN_FAILING_TESTS(...) H_AT(run_failing_tests(H_ARGS(__VA_ARGS__)))

static const char *expect_result(const char *log, const char *name, const char *result) {
    const char *line = find_line(log, format_text("%s *[*/*] %s (*", result, name));
    if (!line) h_fail("expected %s for %s", result, name);
    return line;
}

static void expect_summary(const char *log, int passed, int failed, int skipped) {
    h_expect_has(log, format_text("Tests: %d passed, %d failed, %d skipped", passed, failed, skipped));
}

#define EXPECT_RESULT(log, name, result) H_AT(expect_result((log), (name), (result)))
#define EXPECT_SUMMARY(log, passed, failed, skipped) H_AT(expect_summary((log), (passed), (failed), (skipped)))

static names_t ran_tests(const char *log) {
    names_t names = { NULL, 0, 0 };
    while (*log) {
        const char *end = strchr(log, '\n');
        size_t length = end ? (size_t)(end - log) : strlen(log);
        const char *line = format_text("%.*s", (int)length, log);
        if (glob_match("PASS *[*/*] * (*", line) || glob_match("FAIL *[*/*] * (*", line) ||
            glob_match("SKIP *[*/*] * (*", line)) {
            const char *name = strstr(strchr(line, ']'), " ") + 1;
            const char *close = strstr(name, " (");
            names_add(&names, format_text("%.*s", (int)(close - name), name));
        }
        log += length + (end ? 1 : 0);
    }
    names_sort(&names);
    return names;
}

static names_t listed_tests(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)(argc + 1));
    argv[0] = "--list-tests";
    for (int i = 0; i < argc; ++i) argv[i + 1] = args[i];
    const char *log = h_build(argc + 1, argv);
    names_t names = { NULL, 0, 0 };
    while (*log) {
        const char *end = strchr(log, '\n');
        size_t length = end ? (size_t)(end - log) : strlen(log);
        const char *line = format_text("%.*s", (int)length, log);
        if (line[0] == ' ' && line[1] == ' ' && line[2] != ' ' && line[2] != '(') {
            size_t name_length = strcspn(line + 2, " ");
            names_add(&names, format_text("%.*s", (int)name_length, line + 2));
        }
        log += length + (end ? 1 : 0);
    }
    names_sort(&names);
    return names;
}

#define LISTED_TESTS(...) H_AT(listed_tests(H_ARGS(__VA_ARGS__)))

/* Builds the probe and starts it once with no time limit. */
static void prepare_probe(void) {
    BUILD("--target=probe");
    CHECK(RUN("build/probe" EXE, "--exit", "0").status == 0, "the probe does not run");
}

SCENARIO(runner, listing_builds_nothing) {
    names_t names = LISTED_TESTS("--test-filter=sel_*");
    EXPECT_NAMES(names, "sel_alpha_one", "sel_alpha_two", "sel_beta_one", "sel_plain");
    EXPECT_GONE("build");
}

SCENARIO(runner, listing_shows_every_label) {
    EXPECT_LINE(BUILD("--list-tests", "--test-filter=sel_alpha_one"), "  sel_alpha_one *[sel_a,sel_c]");
}

SCENARIO(runner, listing_reports_an_empty_selection) {
    const char *log = BUILD("--list-tests", "--test-label=no-such-label");
    EXPECT_HAS(log, "(no tests)");
    EXPECT_NO_NAMES(LISTED_TESTS("--test-label=no-such-label"));
}

SCENARIO(runner, list_tests_takes_precedence_over_test) {
    EXPECT_NO_NAMES(ran_tests(BUILD("--test", "--list-tests", "--test-filter=res_pass")));
}

SCENARIO(runner, selection_combines_labels_filters_and_excludes) {
    EXPECT_NAMES(LISTED_TESTS("--test-label=sel_a"), "sel_alpha_one");
    EXPECT_NAMES(LISTED_TESTS("--test-label=sel_c"), "sel_alpha_one");
    EXPECT_NAMES(LISTED_TESTS("--test-label=sel_a", "--test-label=sel_b"), "sel_alpha_one", "sel_alpha_two", "sel_beta_one");
    EXPECT_NAMES(LISTED_TESTS("--test-filter=sel_alpha_*"), "sel_alpha_one", "sel_alpha_two");
    EXPECT_NAMES(LISTED_TESTS("--test-filter=sel_alpha_*", "--test-filter=sel_beta_one"), "sel_alpha_one", "sel_alpha_two", "sel_beta_one");
    EXPECT_NAMES(LISTED_TESTS("--test-filter=sel_?lpha_one"), "sel_alpha_one");
    EXPECT_NAMES(LISTED_TESTS("--test-filter=sel_*", "--test-exclude=*_one"), "sel_alpha_two", "sel_plain");
    EXPECT_NAMES(LISTED_TESTS("--test-filter=sel_*", "--test-exclude=*_one", "--test-exclude=*alpha*"), "sel_plain");
    EXPECT_NAMES(LISTED_TESTS("--test-label=sel_b", "--test-filter=*_one"), "sel_beta_one");
    EXPECT_NAMES(LISTED_TESTS("--test-label=sel_b", "--test-exclude=*beta*"), "sel_alpha_two");
    EXPECT_NO_NAMES(LISTED_TESTS("--test-label=sel_a", "--test-filter=*beta*"));
}

SCENARIO(runner, running_matches_the_selection) {
    const char *log = RUN_TESTS("--test-label=sel_b", "--test-exclude=*beta*");
    EXPECT_NAMES(ran_tests(log), "sel_alpha_two");
    EXPECT_SUMMARY(log, 1, 0, 0);
}

SCENARIO(runner, an_empty_selection_succeeds_without_building) {
    EXPECT_HAS(RUN_TESTS("--test-label=no-such-label"), "no tests selected");
    EXPECT_GONE("build");
}

SCENARIO(runner, selection_flags_need_values) {
    static const char *const flags[] = { "--test-label", "--test-filter", "--test-exclude" };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        EXPECT_HAS(BUILD_FAILS("--test", flags[i]), "requires a value");
        const char *log = BUILD_FAILS("--test", format_text("%s=", flags[i]));
        EXPECT_HAS(log, "requires a value");
        EXPECT_NO_NAMES(ran_tests(log));
    }
}

SCENARIO(runner, unexpected_arguments_stop_the_run) {
    const char *log = BUILD_FAILS("--test", "--test-filter=res_pass", "stray");
    EXPECT_HAS(log, "unexpected argument");
    EXPECT_NO_NAMES(ran_tests(log));
}

SCENARIO(runner, passing_tests_keep_their_output_quiet) {
    const char *log = RUN_TESTS("--test-label=res_pass");
    EXPECT_RESULT(log, "res_pass", "PASS");
    EXPECT_SUMMARY(log, 1, 0, 0);
    EXPECT_LACKS(log, "passing-output");
}

SCENARIO(runner, failing_tests_print_their_output) {
    const char *log = RUN_FAILING_TESTS("--test-label=res_fail");
    EXPECT_HAS(EXPECT_RESULT(log, "res_fail", "FAIL"), "exit code 3");
    EXPECT_HAS(log, "failing-stdout");
    EXPECT_LINE(log, "failing-stderr-no-newline");
    EXPECT_ORDER(log, "Failed tests (logs in build/tests):\n  res_fail\n");
    EXPECT_SUMMARY(log, 0, 1, 0);
}

SCENARIO(runner, skips_do_not_fail_the_run) {
    const char *log = RUN_TESTS("--test-label=res_skip");
    EXPECT_RESULT(log, "res_skip", "SKIP");
    EXPECT_SUMMARY(log, 0, 0, 1);
    EXPECT_LACKS(log, "skipped-output");
    EXPECT_HAS(read_test_log("res_skip"), "skipped-output");
}

SCENARIO(runner, other_nonzero_exits_fail) {
    const char *log = RUN_FAILING_TESTS("--test-label=res_fail_more");
    EXPECT_SUMMARY(log, 0, 4, 0);
    EXPECT_HAS(EXPECT_RESULT(log, "res_skip_code_unset", "FAIL"), "exit code 125");
    EXPECT_HAS(EXPECT_RESULT(log, "res_skip_code_other", "FAIL"), "exit code 3");
    const char *terminated = EXPECT_RESULT(log, "res_terminated", "FAIL");
#ifndef _WIN32
    EXPECT_HAS(terminated, "exit code 143");
#else
    (void)terminated;
#endif
    EXPECT_HAS(EXPECT_RESULT(log, "res_cannot_start", "FAIL"), "could not start no-such-program-for-cbuild-tests");
}

SCENARIO(runner, logs_capture_stdout_and_stderr) {
    RUN_FAILING_TESTS("--test-label=res_fail");
    const char *text = read_test_log("res_fail");
    EXPECT_HAS(text, "failing-stdout");
    EXPECT_HAS(text, "failing-stderr-no-newline");
}

SCENARIO(runner, logs_are_named_after_sanitized_test_names) {
    EXPECT_RESULT(RUN_TESTS("--test-label=env_name"), "weird name/with:chars", "PASS");
    EXPECT_HAS(read_test_log("weird_name_with_chars"), "weird-output");
}

SCENARIO(runner, logs_follow_the_config_output_dir) {
    set_env("RUNNER_CONFIG_OUTDIR", "alt out");
    RUN_TESTS("--test-label=res_pass");
    EXPECT_FILE("alt out/tests/res_pass.log");
    EXPECT_GONE("build/tests/res_pass.log");
}

SCENARIO(runner, reruns_replace_logs_and_tests_inherit_the_environment) {
    set_env("RUNNER_VALUE", "a-long-first-value");
    RUN_TESTS("--test-label=env_var");
    EXPECT_HAS(read_test_log("env_var"), "env=a-long-first-value");
    set_env("RUNNER_VALUE", "short");
    RUN_TESTS("--test-label=env_var");
    const char *text = read_test_log("env_var");
    EXPECT_HAS(text, "env=short");
    EXPECT_LACKS(text, "first-value");
}

SCENARIO(runner, tests_never_see_the_build_programs_stdin) {
    BUILD("--target=probe");
    feed_stdin("input the tests must not see\n");
    CHECK(RUN("build/probe" EXE, "--stdin-empty").status == 5, "programs started from here did not inherit stdin");
    feed_stdin("input the tests must not see\n");
    EXPECT_RESULT(RUN_TESTS("--test-label=env_stdin"), "env_stdin", "PASS");
}

SCENARIO(runner, arguments_arrive_intact) {
    RUN_TESTS("--test-label=env_args");
    EXPECT_ORDER(read_test_log("env_args"), "[two words]\n[]\n[quote\"d]\n[back\\slash]\n[trailing space\\]\n[a*b]\n");
}

SCENARIO(runner, programs_can_live_at_paths_with_spaces) {
    RUN_TESTS("--test-filter=env_spaced_program");
    EXPECT_HAS(read_test_log("env_spaced_program"), "spaced-output");
}

SCENARIO(runner, own_timeouts_kill_the_test) {
    prepare_probe();
    double started = now_seconds();
    const char *log = RUN_FAILING_TESTS("--test-label=time_own");
    CHECK(now_seconds() - started < HANG_LIMIT_SECONDS, "the timed-out test was not killed");
    EXPECT_HAS(EXPECT_RESULT(log, "time_own", "FAIL"), "timed out after 3s");
    EXPECT_HAS(log, "before-timeout");
    EXPECT_HAS(read_test_log("time_own"), "before-timeout");
}

SCENARIO(runner, the_default_timeout_covers_tests_without_their_own) {
    const char *log = RUN_FAILING_TESTS("--test-label=time_default", "--test-timeout=1", "-j2");
    EXPECT_SUMMARY(log, 0, 2, 0);
    EXPECT_HAS(EXPECT_RESULT(log, "time_default", "FAIL"), "timed out after 1s");
    EXPECT_HAS(EXPECT_RESULT(log, "time_negative", "FAIL"), "timed out after 1s");
}

SCENARIO(runner, a_tests_own_timeout_beats_the_default) {
    EXPECT_RESULT(RUN_TESTS("--test-label=time_longer", "--test-timeout=1"), "time_own_longer", "PASS");
}

SCENARIO(runner, timeouts_kill_everything_the_test_started) {
    prepare_probe();
    const char *log = RUN_FAILING_TESTS("--test-label=time_tree");
    EXPECT_HAS(EXPECT_RESULT(log, "time_tree", "FAIL"), "timed out after 2s");
    EXPECT_FILE("sync/started");
    sleep_ms(SURVIVOR_WAIT_MS);
    EXPECT_GONE("sync/survivor");
}

SCENARIO(runner, stopping_the_run_stops_running_tests) {
#ifdef _WIN32
    h_skip("signals are POSIX only");
#else
    prepare_probe();
    int pid = background_start(3, (const char *const[]){ prebuilt_program("runner_cbuild"), "--test", "--test-label=time_tree_stopped" });
    double deadline = now_seconds() + HANG_LIMIT_SECONDS;
    while (!is_file("sync/started")) {
        CHECK(background_is_running(pid), "the build program exited before the test started");
        CHECK(now_seconds() < deadline, "the test never started");
        sleep_ms(10);
    }
    int ended_by = background_stop(pid, SIGTERM);
    CHECK(ended_by == SIGTERM, "the build program was ended by signal %d", ended_by);
    sleep_ms(SURVIVOR_WAIT_MS);
    EXPECT_GONE("sync/survivor");
#endif
}

SCENARIO(runner, tests_run_in_parallel) {
    EXPECT_HAS(RUN_TESTS("--test-label=par", "-j2"), "Test: 2 tests, 2 jobs");
}

SCENARIO(runner, one_job_runs_tests_one_at_a_time) {
    const char *log = RUN_TESTS("--test-label=serial", "-j1");
    EXPECT_HAS(log, "Test: 3 tests, 1 job ");
    EXPECT_SUMMARY(log, 3, 0, 0);
}

SCENARIO(runner, locked_tests_never_overlap) {
    EXPECT_SUMMARY(RUN_TESTS("--test-label=lock_same", "-j4"), 4, 0, 0);
}

SCENARIO(runner, different_locks_do_not_block_each_other) {
    EXPECT_SUMMARY(RUN_TESTS("--test-label=lock_distinct", "-j2"), 2, 0, 0);
}

SCENARIO(runner, cleared_locks_do_not_block) {
    EXPECT_SUMMARY(RUN_TESTS("--test-label=lock_cleared", "-j2"), 2, 0, 0);
}

SCENARIO(runner, a_blocked_test_does_not_hold_up_runnable_ones) {
    EXPECT_SUMMARY(RUN_TESTS("--test-label=lock_pass_over", "-j2"), 3, 0, 0);
}

SCENARIO(runner, unselected_dependencies_are_not_built) {
    RUN_TESTS("--test-label=res_pass");
    EXPECT_GONE("dep-marker.txt");
    EXPECT_GONE("build/versioned" EXE);
}

SCENARIO(runner, added_dependencies_build_before_the_test) {
    EXPECT_RESULT(RUN_TESTS("--test-label=dep_extra"), "dep_extra", "PASS");
}

SCENARIO(runner, non_executable_targets_build_before_args0_runs) {
    EXPECT_RESULT(RUN_TESTS("--test-filter=dep_program_with_target"), "dep_program_with_target", "PASS");
}

SCENARIO(runner, up_to_date_tests_rerun_without_compiling) {
    RUN_TESTS("--test-label=res_pass");
    const char *log = RUN_TESTS("--test-label=res_pass");
    EXPECT_RESULT(log, "res_pass", "PASS");
    EXPECT_STEPS(log, "COMPILE", 0, NULL);
    EXPECT_STEPS(log, "LINK", 0, NULL);
}

SCENARIO(runner, source_edits_rebuild_before_the_test_runs) {
    const char *original = read_text("src/versioned.c");
    RUN_TESTS("--test-filter=dep_versioned");
    settle();
    write_text("src/versioned.c", replace_text(original, "return 0", "return 7"));
    EXPECT_HAS(EXPECT_RESULT(RUN_FAILING_TESTS("--test-filter=dep_versioned"), "dep_versioned", "FAIL"), "exit code 7");
    settle();
    write_text("src/versioned.c", original);
    EXPECT_RESULT(RUN_TESTS("--test-filter=dep_versioned"), "dep_versioned", "PASS");
}

SCENARIO(runner, build_failures_stop_the_run) {
    static const char *const flags[] = { "--test-filter=broken_target", "--test-filter=broken_dep" };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        const char *log = RUN_FAILING_TESTS(flags[i]);
        EXPECT_HAS(log, "Build failed; tests not run.");
        EXPECT_NO_NAMES(ran_tests(log));
        EXPECT_LACKS(log, "Test:");
    }
}

SCENARIO(runner, sections_separate_the_build_from_the_run) {
    RUN_TESTS("--test-label=res_pass");
    const char *log = RUN_TESTS("--test-label=res_pass");
    EXPECT_ORDER(log, SECTION_BAR SECTION_BAR " Build ", STATUS_OK " Up to date.\n\n" SECTION_BAR SECTION_BAR " Test: 1 test, 1 job ");
}

SCENARIO(runner, nested_builds_report_only_their_steps_and_errors) {
    set_env("CBUILD_NESTED", "sub");
    const char *log = BUILD("--target=versioned");
    EXPECT_LACKS(log, SECTION_BAR SECTION_BAR);
    EXPECT_LACKS(log, "Build succeeded");
    CHECK(count_text(log, "build subproject sub") == 1, "the nested build did not name itself once");

    CHECK(*BUILD("--target=versioned") == '\0', "an up-to-date nested build printed output");

    log = BUILD_FAILS("--target=broken");
    EXPECT_HAS(log, "Compilation failed");
    EXPECT_LACKS(log, STATUS_FAIL);
}

SCENARIO(runner, plain_builds_and_cleans_have_sections) {
    EXPECT_LINE(BUILD("--target=versioned"), SECTION_BAR SECTION_BAR " Build *");
    EXPECT_HAS(BUILD("--target=versioned"), STATUS_OK " Up to date.");
    EXPECT_LINE(BUILD("--clean"), SECTION_BAR SECTION_BAR " Clean *");
}

SCENARIO(runner, color_follows_the_environment) {
    set_env("NO_COLOR", "");
    EXPECT_LACKS(RUN_TESTS("--test-label=res_pass"), "\x1b");

    set_env("CLICOLOR_FORCE", "1");
    EXPECT_HAS(RUN_TESTS("--test-label=res_pass"), "\x1b[32mPASS");
    EXPECT_HAS(RUN_TESTS("--test-label=res_skip"), "\x1b[33mSKIP");
    EXPECT_HAS(RUN_FAILING_TESTS("--test-label=res_fail"), "\x1b[31mFAIL");

    set_env("NO_COLOR", "1");
    EXPECT_LACKS(RUN_TESTS("--test-label=res_pass"), "\x1b");
}

SCENARIO(runner, listings_show_the_paths_builds_use) {
    set_env("RUNNER_LATE_OUTDIR", "late");
    static const char *const flags[] = { "--list", "--manifest" };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        const char *log = BUILD(flags[i]);
        EXPECT_HAS(log, "late/probe" EXE);
        EXPECT_LACKS(log, "build/probe" EXE);
    }
    BUILD("--target=versioned");
    EXPECT_FILE("late/versioned" EXE);
}

SCENARIO(runner, unselected_broken_targets_do_not_matter) {
    EXPECT_LACKS(RUN_TESTS("--test-filter=res_pass"), "broken.c");
}

SCENARIO(runner, flag_phases_wrap_the_run) {
    EXPECT_ORDER(RUN_TESTS("--test-label=res_pass", "--before-check", "--after-check"),
                 "before-flag-ran", "Test: 1 test", "Tests: 1 passed", "after-flag-ran");
    const char *log = RUN_FAILING_TESTS("--test-label=res_fail", "--before-check", "--after-check");
    EXPECT_HAS(log, "before-flag-ran");
    EXPECT_LACKS(log, "after-flag-ran");
}
