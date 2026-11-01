#include <string.h>

#include "harness.h"

#define MATH_SOURCE "lib/src/math.c"

void example_setup(void) {
    copy_file(root_path("cbuild.h"), "cbuild.h");
    copy_tree(root_path("example"), "example");
    change_dir("example");
    compile_build_program();
}

static void expect_summary_matches_steps(const char *log) {
    int compiled = count_steps(log, "COMPILE", NULL);
    int linked = count_steps(log, "LINK", NULL);
    int commands = count_steps(log, "COMMAND", NULL) - count_text(log, "build subproject") - count_text(log, "bootstrap math subproject");
    const char *parts = "";
    if (compiled) parts = format_text("%d compiled", compiled);
    if (linked) parts = format_text("%s%s%d linked", parts, *parts ? ", " : "", linked);
    if (commands) parts = format_text("%s%s%d command%s", parts, *parts ? ", " : "", commands, commands == 1 ? "" : "s");
    h_expect_has(log, format_text("Build succeeded (%s, ", parts));
}

#define EXPECT_SUMMARY_MATCHES_STEPS(log) H_AT(expect_summary_matches_steps(log))

SCENARIO(example, builds_nest_their_subproject) {
    const char *log = BUILD();
    EXPECT_SUMMARY_MATCHES_STEPS(log);
    CHECK(strstr(log, SECTION_BAR SECTION_BAR " Setup ") == log, "the bootstrap is not in a setup section");
    EXPECT_ORDER(log, "COMMAND", "bootstrap math subproject", SECTION_BAR SECTION_BAR " Build ");
    CHECK(count_text(log, "Build succeeded") == 1, "the subproject printed its own status");
    CHECK(count_text(log, SECTION_BAR SECTION_BAR " Build") == 1, "the subproject printed its own section");
    CHECK(count_text(log, "build subproject math_project") == 1, "the subproject was not named once");
    EXPECT_LINE(log, "COMPILE *[2/2] src/*");
    EXPECT_LINE(log, "COMPILE *src/math.c*");
}

SCENARIO(example, up_to_date_build_hides_the_subproject) {
    BUILD();
    const char *log = BUILD();
    EXPECT_HAS(log, STATUS_OK " Up to date.");
    EXPECT_LACKS(log, "COMMAND");
    EXPECT_LACKS(log, "Setup");
}

SCENARIO(example, subproject_edits_rebuild_through_the_parent) {
    BUILD();
    settle();
    append_text(MATH_SOURCE, "\n");
    const char *log = BUILD();
    EXPECT_HAS(log, "build subproject math_project");
    EXPECT_SUMMARY_MATCHES_STEPS(log);
}

SCENARIO(example, broken_subproject_fails_once) {
    BUILD();
    settle();
    append_text(MATH_SOURCE, "\nnot valid c\n");
    const char *log = BUILD_FAILS();
    EXPECT_HAS(log, "Compilation failed");
    EXPECT_HAS(log, STATUS_FAIL " Build failed.");
    CHECK(count_text(log, STATUS_FAIL) == 1, "expected one final failure line");
}

SCENARIO(example, clean_is_nested) {
    BUILD();
    const char *log = BUILD("--clean");
    CHECK(count_text(log, "Clean complete") == 1, "the subproject clean was not nested");
    CHECK(count_text(log, "Cleaning build outputs") == 1, "the subproject clean printed its steps");
}

SCENARIO(example, run_subcommand_prints_its_output) {
    const char *log = BUILD("--run=run");
    EXPECT_HAS(log, "2 + 3 = 5");
    EXPECT_HAS(log, "Run: run");
}

SCENARIO(example, registered_tests_pass) {
    const char *log = BUILD("--test");
    EXPECT_LINE(log, "PASS *math_arithmetic*");
    EXPECT_LINE(log, "PASS *main_runs*");
    EXPECT_LINE(log, "SKIP *math_round_trips*");
    EXPECT_HAS(log, " 0 failed");
}

SCENARIO(example, slow_tests_run_on_request) {
    set_env("MATH_SLOW_TESTS", "1");
    EXPECT_HAS(BUILD("--test", "--test-label=slow"), "Tests: 1 passed");
}
