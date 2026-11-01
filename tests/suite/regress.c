#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define APP "dist/regress_app" EXE
#define GENERATED "build/generated.h"

#ifdef _WIN32
#define LIBRARY "build/util.lib"
#define LIBRARY_NAME "util.lib"
#else
#define LIBRARY "build/libutil.a"
#define LIBRARY_NAME "libutil.a"
#endif

void regress_setup(void) {
    copy_tree(root_path("tests/fixtures/regress"), ".");
    make_dir("dist");
    write_text("dist/README.txt", "not a build output\n");
    use_program(prebuilt_program("regress_cbuild"));
}

static void expect_noop(void) {
    const char *log = h_build(0, NULL);
    static const char *const labels[] = { "COMPILE", "LINK", "FILE_DEP", "COMMAND" };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        if (count_steps(log, labels[i], NULL) != 0) h_fail("an up-to-date build did work");
    }
}

static void expect_app_output(const char *expected) {
    outcome_t outcome = h_run(1, (const char *const[]){ APP });
    if (outcome.status != 0) h_fail("the app failed");
    if (!find_line(outcome.log, expected)) h_fail("expected the app to print '%s'", expected);
}

#define EXPECT_NOOP() H_AT(expect_noop())
#define EXPECT_APP_OUTPUT(expected) H_AT(expect_app_output(expected))

static void delete_files_with_suffix(const char *directory, const char *suffix) {
    names_t files = files_with_suffix(directory, suffix);
    for (int i = 0; i < files.count; ++i) delete_file(files.items[i]);
}

SCENARIO(regress, generated_files_respect_dependencies) {
    BUILD("--target=regress_app");
    EXPECT_FILE("build/gen" EXE);
    EXPECT_FILE(GENERATED);
}

SCENARIO(regress, explicit_output_paths) {
    BUILD();
    EXPECT_FILE(APP);
    EXPECT_GONE("build/regress_app" EXE);
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=0");
}

static const char *argument_ending_with(const json_t *arguments, const char *suffix) {
    size_t suffix_length = strlen(suffix);
    for (int i = 0; i < arguments->count; ++i) {
        const char *text = arguments->items[i]->text;
        size_t length = strlen(text);
        if (length >= suffix_length && strcmp(text + length - suffix_length, suffix) == 0) return text;
    }
    return NULL;
}

SCENARIO(regress, compile_commands_use_target_settings) {
    BUILD();
    BUILD("--compile-commands", "-j2");
    json_t *entries = json_parse(read_text("build/compile_commands.json"));
    CHECK(entries && entries->type == JSON_ARRAY, "compile_commands.json is not a JSON array");

    names_t files = { NULL, 0, 0 };
    names_t compiler = split_flags(getenv("CC"));
    const json_t *util = NULL;
    const json_t *app = NULL;
    for (int i = 0; i < entries->count; ++i) {
        const json_t *file = json_get(entries->items[i], "file");
        const json_t *arguments = json_get(entries->items[i], "arguments");
        CHECK(file && file->type == JSON_STRING, "an entry has no file");
        CHECK(arguments && arguments->type == JSON_ARRAY, "%s has no arguments", file->text);
        CHECK(arguments->count > compiler.count, "%s has too few arguments", file->text);
        for (int j = 0; j < compiler.count; ++j) {
            CHECK(strcmp(arguments->items[j]->text, compiler.items[j]) == 0, "%s does not start with the compiler", file->text);
        }
        names_add(&files, file->text);
        if (strcmp(file->text, "space dir/src/util.c") == 0) util = arguments;
        if (strcmp(file->text, "src/app.c") == 0) app = arguments;
    }
    names_sort(&files);
    EXPECT_NAMES(files, "space dir/src/util.c", "src/app.c", "src/gen.c");
    CHECK(argument_ending_with(util, "-DTARGET_CC=1"), "per-target compiler flags are missing");
    CHECK(!argument_ending_with(app, "-DTARGET_CC=1"), "per-target compiler flags leaked into another target");
    CHECK(argument_ending_with(app, "Ispace dir/include"), "the include directory is missing");
}

SCENARIO(regress, repeated_builds_are_noops) {
    BUILD();
    for (int round = 0; round < 3; ++round) EXPECT_NOOP();
}

SCENARIO(regress, edits_within_the_same_second_are_noticed) {
    BUILD();
    double started = now_seconds();
    settle();
    append_text("src/app.c", "/* edited */\n");
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, "app.c");
    settle();
    touch_file("space dir/include/extra.h");
    log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, "util.c");
    EXPECT_STEPS(log, "COMPILE", 1, NULL);
    if (now_seconds() - started >= 1.0) printf("note: machine too slow to exercise sub-second edits\n");
}

SCENARIO(regress, header_edits_rebuild_their_includers) {
    BUILD();
    settle();
    touch_file("space dir/include/spaced.h");
    EXPECT_STEPS(BUILD(), "COMPILE", 2, NULL);
}

SCENARIO(regress, missing_header_fails_until_restored) {
    BUILD();
    rename_path("space dir/include/extra.h", "extra.h.saved");
    BUILD_FAILS();
    BUILD_FAILS();
    rename_path("extra.h.saved", "space dir/include/extra.h");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
}

SCENARIO(regress, dropped_header_does_not_break_the_build) {
    BUILD();
    settle();
    replace_in_file("space dir/src/util.c", "#include \"extra.h\"\n", "");
    delete_file("space dir/include/extra.h");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
    EXPECT_APP_OUTPUT("generated=41 util=3 mode=0");
    EXPECT_NOOP();
}

SCENARIO(regress, failed_compilation_recovers) {
    BUILD();
    settle();
    const char *original = read_text("src/app.c");
    write_text("src/app.c", format_text("%s#error broken on purpose\n", original));
    BUILD_FAILS();
    BUILD_FAILS();
    settle();
    write_text("src/app.c", original);
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, NULL);
    EXPECT_STEPS(log, "LINK", 1, NULL);
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=0");
}

SCENARIO(regress, changing_a_define_rebuilds_its_target) {
    BUILD();
    set_env("REGRESS_APP_DEFINE", "MODE=2");
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, "app.c");
    EXPECT_STEPS(log, "COMPILE", 1, NULL);
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=2");
    EXPECT_NOOP();
    unset_env("REGRESS_APP_DEFINE");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
}

SCENARIO(regress, changing_target_cflags_rebuilds_and_relinks) {
    BUILD();
    set_env("REGRESS_UTIL_CFLAGS", "-O1");
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, "util.c");
    EXPECT_STEPS(log, "COMPILE", 1, NULL);
    EXPECT_STEPS(log, "LINK", 1, "regress_app");
    unset_env("REGRESS_UTIL_CFLAGS");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
}

SCENARIO(regress, compiler_arguments_do_not_change_its_style) {
    set_env("CC", format_text("%s -Ivendor/include -DMENTIONS_CL=1", getenv("CC")));
    BUILD();
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=0");
}

SCENARIO(regress, environment_cflags_reach_the_compiler) {
    BUILD();
    const char *inherited = getenv("CFLAGS");
    const char *original = inherited ? format_text("%s", inherited) : "";
    set_env("CFLAGS", with_inherited("CFLAGS", "-DMODE=2"));
    EXPECT_STEPS(BUILD(), "COMPILE", 3, NULL);
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=2");
    EXPECT_NOOP();
    set_env("CFLAGS", original);
    BUILD();
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=0");
}

SCENARIO(regress, environment_cppflags_reach_the_compiler) {
    BUILD();
    set_env("CPPFLAGS", with_inherited("CPPFLAGS", "-DMODE=3"));
    BUILD();
    EXPECT_APP_OUTPUT("generated=41 util=7 mode=3");
}

SCENARIO(regress, environment_ldflags_reach_the_linker) {
    BUILD();
    const char *inherited = getenv("LDFLAGS");
    const char *original = inherited ? format_text("%s", inherited) : "";
#ifdef _WIN32
    const char *missing = "cbuild_no_such_library.lib";
#else
    const char *missing = "-lcbuild_no_such_library";
#endif
    set_env("LDFLAGS", with_inherited("LDFLAGS", missing));
    BUILD_FAILS();
    set_env("LDFLAGS", original);
    BUILD();
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_object_comes_back) {
    BUILD();
#ifdef _WIN32
    delete_files_with_suffix("build/obj_util", ".obj");
#else
    delete_files_with_suffix("build/obj_util", ".o");
#endif
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_depfile_comes_back) {
    BUILD();
    delete_files_with_suffix("build/obj_regress_app", ".d");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_signature_comes_back) {
    BUILD();
    delete_files_with_suffix("build/obj_regress_app", ".sig");
    EXPECT_STEPS(BUILD(), "COMPILE", 1, NULL);
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_library_is_relinked) {
    BUILD();
    delete_file(LIBRARY);
    EXPECT_STEPS(BUILD(), "COMPILE", 0, NULL);
    EXPECT_FILE(LIBRARY);
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_executable_is_relinked) {
    BUILD();
    delete_file(APP);
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 0, NULL);
    EXPECT_STEPS(log, "LINK", 1, NULL);
    EXPECT_FILE(APP);
    EXPECT_NOOP();
}

SCENARIO(regress, deleted_generated_file_is_regenerated) {
    BUILD();
    delete_file(GENERATED);
    EXPECT_STEPS(BUILD(), "FILE_DEP", 1, NULL);
    EXPECT_FILE(GENERATED);
    EXPECT_NOOP();
}

SCENARIO(regress, rebuilt_generator_regenerates) {
    BUILD();
    settle();
    replace_in_file("src/gen.c", "GENERATED_VALUE 41", "GENERATED_VALUE 42");
    const char *log = BUILD();
    EXPECT_STEPS(log, "COMPILE", 1, "gen.c");
    EXPECT_STEPS(log, "FILE_DEP", 1, NULL);
    EXPECT_STEPS(log, "COMPILE", 1, "app.c");
    EXPECT_APP_OUTPUT("generated=42 util=7 mode=0");
    EXPECT_NOOP();
}

SCENARIO(regress, command_cycles_are_reported) {
    set_env("REGRESS_COMMAND_CYCLE", "1");
    outcome_t outcome = CBUILD("--target=cycle");
    CHECK(outcome.status > 0 && outcome.status < 128, "a command cycle exited with status %d", outcome.status);
    EXPECT_HAS(outcome.log, "circular command dependency");
}

SCENARIO(regress, clean_removes_only_outputs) {
    BUILD();
    make_dir("precious");
    write_text("precious/keep.txt", "keep me\n");
    make_dir("build/nested");
    link_dir("build/dirlink", "precious");
    link_dir("build/nested/deeplink", "precious");
#ifndef _WIN32
    link_file("build/filelink", "precious/keep.txt");
#endif
    BUILD("--clean");
    EXPECT_GONE("build");
    EXPECT_GONE(APP);
    EXPECT_GONE(APP ".link.sig");
    EXPECT_FILE("dist/README.txt");
    EXPECT_FILE("vendor/data.txt");
    EXPECT_FILE("precious/keep.txt");
}

SCENARIO(regress, clean_refuses_the_project_directory) {
    BUILD();
    make_dir("precious");
    write_text("precious/keep.txt", "keep me\n");
    set_env("REGRESS_OUTDIR", ".");
    EXPECT_HAS(BUILD("--clean"), "not cleaning");
    EXPECT_FILE("build.c");
    EXPECT_FILE("src/app.c");
    EXPECT_FILE("precious/keep.txt");
}

SCENARIO(regress, clean_through_a_linked_output_directory) {
    make_dir("real-build");
    link_dir("build", "real-build");
    BUILD();
    EXPECT_FILE("real-build/" LIBRARY_NAME);
    write_text("real-build/stray.txt", "stray\n");
    EXPECT_HAS(BUILD("--clean"), "not cleaning");
    EXPECT_FILE("real-build/stray.txt");
    EXPECT_GONE("real-build/" LIBRARY_NAME);
    EXPECT_GONE("real-build/obj_util");
    EXPECT_GONE(APP);
    remove_dir_link("build");
}
