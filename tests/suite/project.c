#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define APP "build/test_app" EXE

#ifdef _WIN32
#define STATIC_LIBRARY "build/math.lib"
#define SHARED_LIBRARY "build/shared.dll"
#elif defined(__APPLE__)
#define STATIC_LIBRARY "build/libmath.a"
#define SHARED_LIBRARY "build/libshared.dylib"
#else
#define STATIC_LIBRARY "build/libmath.a"
#define SHARED_LIBRARY "build/libshared.so"
#endif

void project_setup(void) {
    copy_tree(root_path("tests/fixtures/project"), ".");
    copy_file(root_path("cbuild.h"), "cbuild.h");
    compile_build_program();
}

SCENARIO(project, version_prints_the_header_version) {
    EXPECT_LINE(BUILD("--version"), CBUILD_VERSION);
}

SCENARIO(project, help_lists_builtin_and_custom_flags) {
    const char *log = BUILD("--help");
    EXPECT_HAS(log, "--target");
    EXPECT_HAS(log, "--test-filter");
    EXPECT_HAS(log, "--feature");
    EXPECT_HAS(log, "Build test_app with FEATURE_VALUE=1");
}

SCENARIO(project, list_shows_every_target) {
    const char *log = BUILD("--list");
    EXPECT_LINE(log, "  test_app *[executable]*");
    EXPECT_LINE(log, "  math *[static_lib]*");
    EXPECT_LINE(log, "  shared *[shared_lib]*");
    EXPECT_LINE(log, "  generated_header *[file_dep]*");
    EXPECT_LINE(log, "  all *[dummy]*");
}

SCENARIO(project, graph_shows_dependencies) {
    const char *log = BUILD("--graph");
    EXPECT_HAS(log, "math");
    EXPECT_HAS(log, "generated_header");
}

SCENARIO(project, deps_lists_reverse_dependencies) {
    const char *log = BUILD("--deps=math");
    EXPECT_HAS(log, "test_app");
    EXPECT_LACKS(log, "shared");
}

SCENARIO(project, manifest_lists_targets_by_kind) {
    const char *log = BUILD("--manifest");
    EXPECT_HAS(log, "static_lib math");
    EXPECT_HAS(log, "shared_lib shared");
    EXPECT_HAS(log, "executable test_app");
}

SCENARIO(project, unknown_names_are_rejected) {
    BUILD_FAILS("--target=no_such_target");
    BUILD_FAILS("--run=no_such_subcommand");
    EXPECT_HAS(BUILD_FAILS("no-such-subcommand"), "unexpected argument");
    EXPECT_HAS(BUILD_FAILS("--no-such-flag"), "no-such-flag");
}

SCENARIO(project, first_build_produces_every_output) {
    BUILD("--compile-commands", "-j2");
    EXPECT_FILE("build/generated.h");
    EXPECT_FILE("build/command-dependency.txt");
    EXPECT_HAS(read_text("build/command-main.txt"), "dependency-ran-first");
    EXPECT_FILE(APP);
    EXPECT_FILE(STATIC_LIBRARY);
    EXPECT_FILE(SHARED_LIBRARY);
    EXPECT_FILE("build/compile_commands.json");
}

SCENARIO(project, app_uses_everything_it_links) {
    BUILD();
    outcome_t outcome = RUN(APP, "--self-test");
    CHECK(outcome.status == 0, "the self test failed");
    EXPECT_HAS(outcome.log, "sum=5 product=12 generated=7 feature=0");
}

SCENARIO(project, compile_commands_are_valid) {
    BUILD("--compile-commands", "-j2");
    json_t *entries = json_parse(read_text("build/compile_commands.json"));
    CHECK(entries && entries->type == JSON_ARRAY, "compile_commands.json is not a JSON array");
    names_t files = { NULL, 0, 0 };
    for (int i = 0; i < entries->count; ++i) {
        const json_t *directory = json_get(entries->items[i], "directory");
        const json_t *file = json_get(entries->items[i], "file");
        const json_t *arguments = json_get(entries->items[i], "arguments");
        CHECK(directory && directory->type == JSON_STRING, "entry %d has no directory", i);
        CHECK(file && file->type == JSON_STRING, "entry %d has no file", i);
        CHECK(arguments && arguments->type == JSON_ARRAY && arguments->count > 0, "%s has no arguments", file->text);
        names_add(&files, file->text);
    }
    names_sort(&files);
    EXPECT_NAMES(files, "src/app.c", "src/math.c", "src/math_extra.c", "src/shared.c");
}

SCENARIO(project, up_to_date_build_does_nothing) {
    BUILD("-j2");
    const char *log = BUILD("-j2");
    EXPECT_STEPS(log, "COMPILE", 0, NULL);
    EXPECT_STEPS(log, "LINK", 0, NULL);
}

SCENARIO(project, flags_can_change_defines_before_the_build) {
    BUILD();
    EXPECT_STEPS(BUILD("--feature", "--target=test_app"), "COMPILE", 1, "app.c");
    EXPECT_HAS(RUN(APP, "--self-test").log, "feature=1");
}

SCENARIO(project, run_flag_runs_a_subcommand) {
    EXPECT_HAS(BUILD("--feature", "--run=app"), "feature=1");
}

SCENARIO(project, bare_name_runs_a_subcommand) {
    EXPECT_HAS(BUILD("--feature", "app"), "feature=1");
}

SCENARIO(project, generated_file_input_regenerates) {
    BUILD("--target=test_app");
    settle();
    touch_file("inputs/schema.txt");
    const char *log = BUILD("--target=test_app");
    EXPECT_STEPS(log, "FILE_DEP", 1, NULL);
    EXPECT_STEPS(log, "COMPILE", 1, "app.c");
}

SCENARIO(project, build_program_rebuilds_itself) {
    settle();
    touch_file("build.c");
    const char *log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_HAS(log, "test_app");
    EXPECT_LACKS(BUILD("--list"), "Detected changes");
}

static const char *fake_compiler_command(const char *name, const char *mode) {
    return format_text("\"%s\" --fake-compiler %s %s", h_self(), name, mode);
}

static void mark_build_program_stale(void) {
    settle();
    touch_file("build.c");
}

SCENARIO(project, rebuild_defaults_without_compiler_settings) {
    mark_build_program_stale();
    unset_env("CC");
    unset_env("CC_FOR_BUILD");
    const char *log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_LACKS(log, "continuing");
    EXPECT_LACKS(BUILD("--list"), "Detected changes");
}

SCENARIO(project, rebuild_prefers_cc_for_build_to_cc) {
    mark_build_program_stale();
    set_env("CC_FOR_BUILD", fake_compiler_command("host", "fail"));
    set_env("CC", fake_compiler_command("target", "fail"));
    BUILD_FAILS("--list");
    EXPECT_ORDER(read_text("compilers-used.txt"), "host\n");
    EXPECT_LACKS(read_text("compilers-used.txt"), "target");

    unset_env("CC_FOR_BUILD");
    BUILD_FAILS("--list");
    EXPECT_ORDER(read_text("compilers-used.txt"), "host\ntarget\n");
}

SCENARIO(project, rebuilt_program_comes_from_the_chosen_compiler) {
#ifdef _WIN32
    h_skip("the fake compiler forwards GCC-style arguments, which cl does not take");
#else
    set_env("FAKE_COMPILER_REAL", getenv("CC"));
    mark_build_program_stale();
    set_env("CC", fake_compiler_command("wrapper", "forward"));
    const char *log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_LACKS(log, "continuing");
    EXPECT_LINE(log, "  test_app *[executable]*");
    EXPECT_HAS(read_text("compilers-used.txt"), "wrapper");
    EXPECT_LACKS(BUILD("--list"), "Detected changes");
#endif
}

SCENARIO(project, failed_rebuild_stops_and_keeps_the_old_program) {
    const char *real_compiler = format_text("%s", getenv("CC"));
    mark_build_program_stale();
    set_env("CC_FOR_BUILD", "no-such-compiler-for-cbuild-tests");
    const char *log = BUILD_FAILS("--list");
    EXPECT_HAS(log, "could not rebuild");
    EXPECT_NO_LINE(log, "  test_app *[executable]*");
    EXPECT_FILE("cbuild" EXE);
    EXPECT_GONE("cbuild" EXE ".old");

    set_env("CC_FOR_BUILD", real_compiler);
    log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_LINE(log, "  test_app *[executable]*");
    EXPECT_LACKS(BUILD("--list"), "Detected changes");
}

SCENARIO(project, syntax_error_in_the_build_program_stops_the_run) {
    unset_env("CC_FOR_BUILD");
    const char *original = read_text("build.c");
    settle();
    write_text("build.c", format_text("%snot valid c\n", original));
    const char *log = BUILD_FAILS("--list");
    EXPECT_HAS(log, "could not rebuild");
    EXPECT_NO_LINE(log, "  test_app *[executable]*");
    EXPECT_FILE("cbuild" EXE);

    settle();
    write_text("build.c", original);
    log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_LINE(log, "  test_app *[executable]*");
}

SCENARIO(project, unstartable_rebuild_keeps_the_old_program) {
    mark_build_program_stale();
    set_env("CC_FOR_BUILD", fake_compiler_command("cross", "unstartable"));
    const char *log = BUILD("--list");
    EXPECT_HAS(read_text("compilers-used.txt"), "cross");
    EXPECT_HAS(log, "continuing with the old build program");
    EXPECT_HAS(log, "CC_FOR_BUILD");
    EXPECT_LINE(log, "  test_app *[executable]*");
    EXPECT_GONE("cbuild" EXE ".old");
    EXPECT_LINE(BUILD("--list"), "  test_app *[executable]*");
}

SCENARIO(project, registered_tests_run_against_real_targets) {
    const char *log = BUILD("--test", "-j2");
    EXPECT_LINE(log, "PASS *app_self_test*");
    EXPECT_LINE(log, "PASS *app_by_path*");
    EXPECT_LINE(log, "SKIP *app_skip*");
    EXPECT_HAS(log, "Tests: 2 passed, 0 failed, 1 skipped");
    EXPECT_FILE(SHARED_LIBRARY);
}

SCENARIO(project, flags_can_register_tests) {
    EXPECT_LACKS(BUILD("--list-tests"), "app_fails");
    outcome_t outcome = CBUILD("--failing-tests", "--fail-code=7", "--test", "--test-label=failing");
    CHECK(outcome.status == 1, "the failing tests exited with status %d", outcome.status);
    EXPECT_LINE(outcome.log, "FAIL *app_fails*exit code 7*");
    EXPECT_LINE(outcome.log, "FAIL *app_timeout*timed out after 1s*");
    EXPECT_HAS(outcome.log, "Tests: 0 passed, 2 failed, 0 skipped");
    EXPECT_FILE("build/tests/app_fails.log");
}

SCENARIO(project, spawn_helper_captures_output) {
    EXPECT_HAS(BUILD("--spawn-check"), "spawn rc=4 output=sum=5");
}

SCENARIO(project, clean_removes_outputs) {
    BUILD();
    EXPECT_FILE(APP);
    BUILD("--clean");
    EXPECT_GONE(APP);
    EXPECT_FILE("inputs/schema.txt");
}
