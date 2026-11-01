#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

void api_setup(void) {}

enum { CAPTURE_CAPACITY = 128 * 1024 };

/* Log messages collected by a logger callback. */
typedef struct {
    char text[CAPTURE_CAPACITY];
    size_t length;
} capture_t;

static capture_t capture;

static void clear_capture(void) {
    capture.length = 0;
    capture.text[0] = '\0';
}

static void capture_message(const char *message) {
    size_t length = strlen(message);
    CHECK(capture.length + length + 2 <= sizeof(capture.text), "the captured log is too long");
    memcpy(capture.text + capture.length, message, length);
    capture.length += length;
    capture.text[capture.length++] = '\n';
    capture.text[capture.length] = '\0';
}

static void capture_all(void *user_data, cbuild_log_level_t level, const char *message) {
    (void)user_data;
    (void)level;
    capture_message(message);
}

static void capture_sections(void *user_data, cbuild_log_level_t level, const char *message) {
    (void)user_data;
    if (level == CBUILD_LOG_SECTION) capture_message(message);
    CHECK(message[0] != '\0', "a log message was empty");
}

static cbuild_context_t *new_capturing_context(cbuild_log_fn logger) {
    cbuild_context_t *ctx = cbuild_context_new();
    CHECK(ctx != NULL, "no context");
    clear_capture();
    cbuild_set_logger(ctx, logger, NULL);
    cbuild_set_output_dir(ctx, "out");
    return ctx;
}

static void count_messages(void *user_data, cbuild_log_level_t level, const char *message) {
    (void)level;
    (void)message;
    (*(int *)user_data)++;
}

static void count_callback(void *user_data) {
    (*(int *)user_data)++;
}

SCENARIO(api, version_macros_agree) {
    const char *expected = format_text("v%d.%d.%d", CBUILD_VERSION_MAJOR, CBUILD_VERSION_MINOR, CBUILD_VERSION_PATCH);
    CHECK(strcmp(CBUILD_VERSION, expected) == 0, "CBUILD_VERSION is %s, its components say %s", CBUILD_VERSION, expected);
}

SCENARIO(api, user_data_and_logger) {
    cbuild_context_t *ctx = cbuild_context_new();
    CHECK(ctx != NULL, "no context");
    int user_value = 42;
    cbuild_set_user_data(ctx, &user_value);
    CHECK(cbuild_get_user_data(ctx) == &user_value, "user data was not kept");

    int messages = 0;
    cbuild_set_logger(ctx, count_messages, &messages);
    const char *args[] = { "cbuild", "--list" };
    CHECK(cbuild_run(ctx, 2, (char **)args) == 0, "--list failed");
    CHECK(messages > 0, "the logger was not called");
    cbuild_context_free(ctx);
}

SCENARIO(api, file_helpers) {
    write_text("file.txt", "contents\n");
    make_dir("directory");
    CHECK(cbuild_file_exists("file.txt"), "file.txt should exist");
    CHECK(!cbuild_file_exists("directory"), "a directory is not a file");
    CHECK(cbuild_dir_exists("directory"), "directory should exist");
    CHECK(!cbuild_dir_exists("file.txt"), "a file is not a directory");
    CHECK(!cbuild_file_exists(NULL) && !cbuild_dir_exists(""), "empty paths do not exist");

    CHECK(cbuild_remove_file("file.txt") == 0, "could not remove file.txt");
    CHECK(!cbuild_file_exists("file.txt"), "file.txt survived");
    CHECK(cbuild_remove_file("file.txt") == 0, "removing a missing file is not an error");
    CHECK(cbuild_remove_file(NULL) == -1, "NULL is an error");
}

static void expect_expansion(const char *pattern, int count, const char *const *expected) {
    char **files = NULL;
    int file_count = 0;
    CHECK(cbuild_expand_wildcard(pattern, &files, &file_count) == 0, "%s failed", pattern);
    names_t actual = { NULL, 0, 0 };
    for (int i = 0; i < file_count; ++i) {
        names_add(&actual, format_text("%s", files[i]));
        free(files[i]);
    }
    free(files);
    printf("pattern %s\n", pattern);
    h_expect_names(actual, count, expected);
}

#define EXPECT_EXPANSION(pattern, ...) H_AT(expect_expansion((pattern), H_ARGS(__VA_ARGS__)))

SCENARIO(api, wildcards) {
    CHECK(cbuild_match_wildcard("*.c", "build.c"), "*.c should match build.c");
    CHECK(!cbuild_match_wildcard("*.h", "build.c"), "*.h should not match build.c");
    CHECK(cbuild_match_wildcard("b?ild.c", "build.c"), "? should match one character");

    make_dirs("tree/sub/deep");
    make_dirs("lib/a/src");
    make_dirs("lib/b/src");
    write_text("tree/two.c", "");
    write_text("tree/one.c", "");
    write_text("tree/skip.h", "");
    write_text("tree/sub/three.c", "");
    write_text("tree/sub/deep/four.c", "");
    write_text("lib/a/src/x.h", "");
    write_text("lib/b/src/y.h", "");
    write_text("lib/b/src/y.c", "");

    EXPECT_EXPANSION("tree/*.c", "tree/one.c", "tree/two.c");
    EXPECT_EXPANSION("tree/**/*.c", "tree/one.c", "tree/sub/deep/four.c", "tree/sub/three.c", "tree/two.c");
    EXPECT_EXPANSION("lib/*/src/*.h", "lib/a/src/x.h", "lib/b/src/y.h");
    H_AT(expect_expansion("tree/*.none", 0, NULL));
}

SCENARIO(api, remove_dir_does_not_follow_links) {
    make_dirs("precious");
    write_text("precious/keep.txt", "contents\n");
    make_dirs("tree/nested");
    write_text("tree/nested/file.txt", "contents\n");
    link_dir("tree/link", "precious");
    link_dir("tree/nested/deep", "precious");
    link_dir("top", "precious");
#ifndef _WIN32
    link_file("tree/dangling", "no-such-target");
#endif

    CHECK(cbuild_remove_dir("top") == 0, "could not remove a link to a directory");
    EXPECT_GONE("top");
    EXPECT_FILE("precious/keep.txt");

    CHECK(cbuild_remove_dir("tree") == 0, "could not remove the tree");
    EXPECT_GONE("tree");
    EXPECT_FILE("precious/keep.txt");

    CHECK(cbuild_remove_dir("no-such-dir") == 0, "removing a missing directory is not an error");
    CHECK(cbuild_remove_dir(NULL) == -1 && cbuild_remove_dir("") == -1, "empty paths are errors");
}

static void expect_flag(int argc, const char *const *argv, const char *name, char short_name, const char *expected) {
    const char *actual = cbuild_get_flag_value(argc, (char **)argv, name, short_name);
    int same = expected ? actual && strcmp(actual, expected) == 0 : actual == NULL;
    if (!same) h_fail("flag %s is '%s', expected '%s'", name ? name : "?", actual ? actual : "(none)", expected ? expected : "(none)");
}

#define EXPECT_FLAG(argc, argv, name, short_name, expected) \
    H_AT(expect_flag((argc), (argv), (name), (short_name), (expected)))

SCENARIO(api, flag_values) {
    const char *forms[] = { "cbuild", "--mode=fast", "-j", "4", "--out", "dir", "--", "--mode=late" };
    EXPECT_FLAG(8, forms, "mode", 0, "fast");
    EXPECT_FLAG(8, forms, "jobs", 'j', "4");
    EXPECT_FLAG(8, forms, "out", 0, "dir");
    EXPECT_FLAG(8, forms, "missing", 0, NULL);

    const char *repeated[] = { "cbuild", "--mode=first", "--mode", "last" };
    EXPECT_FLAG(4, repeated, "mode", 0, "last");

    const char *mixed[] = { "cbuild", "-j2", "--jobs=3" };
    EXPECT_FLAG(3, mixed, "jobs", 'j', "3");
    EXPECT_FLAG(2, mixed, "jobs", 'j', "2");
    EXPECT_FLAG(3, mixed, NULL, 'x', NULL);
    EXPECT_FLAG(3, mixed, NULL, 'j', "2");

    const char *prefixes[] = { "cbuild", "--mode-extra=x", "--modes", "y" };
    EXPECT_FLAG(4, prefixes, "mode", 0, NULL);

    const char *dangling[] = { "cbuild", "--mode=kept", "--mode" };
    EXPECT_FLAG(3, dangling, "mode", 0, "kept");
    EXPECT_FLAG(2, dangling + 1, "mode", 0, NULL);

    const char *empty[] = { "cbuild", "--mode=" };
    EXPECT_FLAG(2, empty, "mode", 0, "");

    const char *program_name[] = { "--mode=program" };
    EXPECT_FLAG(1, program_name, "mode", 0, NULL);
}

SCENARIO(api, callback_commands) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    int first = 0;
    int second = 0;
    command_t *callback = cbuild_command_function(ctx, "first", count_callback, &first);
    command_t *dependent = cbuild_command_function(ctx, "second", count_callback, &second);
    CHECK(callback && dependent, "commands were not created");
    cbuild_command_add_dependency(ctx, dependent, callback);
    CHECK(cbuild_run_command(ctx, dependent) == 0, "the command failed");
    CHECK(first == 1 && second == 1, "commands ran %d and %d times", first, second);
    cbuild_context_free(ctx);
}

SCENARIO(api, configuration_setters) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    config_t *config = cbuild_config_new(ctx, "Settings");
    CHECK(config != NULL, "no config");
    cbuild_config_add_cflags(ctx, config, "-DSETTINGS");
    cbuild_config_add_ldflags(ctx, config, "-s");
    cbuild_config_add_define(ctx, config, "SETTINGS=1");
    cbuild_config_add_include(ctx, config, "include");
    cbuild_config_add_libdir(ctx, config, "lib");
    cbuild_config_add_linklib(ctx, config, "m");
    cbuild_config_set_opt(ctx, config, 1);
    cbuild_config_set_debug(ctx, config, 1);
    cbuild_config_set_lto(ctx, config, 0);
    cbuild_config_set_pic(ctx, config, 1);
    cbuild_config_set_warnings(ctx, config, 1);
    cbuild_config_set_std(ctx, config, "c11");
    cbuild_config_set_runtime(ctx, config, "dynamic");
    cbuild_config_set_output_dir(ctx, config, "out");
    cbuild_config_set_freestanding(ctx, config, 0);
    cbuild_config_enable_sanitizers(ctx, config, 1);
    cbuild_config_disable_sanitizers(ctx, config, 1);
    cbuild_set_active_config(ctx, config);
    CHECK(cbuild_config_default_debug(ctx) != NULL, "no debug config");
    CHECK(cbuild_config_default_release(ctx) != NULL, "no release config");
    CHECK(cbuild_config_default_wasm32(ctx) != NULL, "no wasm32 config");

    target_t *phony = cbuild_dummy_target(ctx, "phony");
    CHECK(phony != NULL, "no target");
    cbuild_target_set_config(ctx, phony, config);
    CHECK(cbuild_build(ctx, "phony") == 0, "a target with every setting did not build");
    cbuild_context_free(ctx);
}

SCENARIO(api, invalid_arguments_set_the_last_error) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    CHECK(cbuild_add_source(ctx, NULL, "missing.c") == -1, "a NULL target was accepted");
    CHECK(cbuild_get_last_error(ctx) != NULL, "no last error");
    cbuild_context_free(ctx);
}

SCENARIO(api, reset_allows_a_new_graph) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    CHECK(cbuild_dummy_target(ctx, "before_reset") != NULL, "no target");
    cbuild_reset(ctx);
    cbuild_set_logger(ctx, capture_all, NULL);
    cbuild_set_output_dir(ctx, "out");
    CHECK(cbuild_dummy_target(ctx, "after_reset") != NULL, "no target after a reset");
    CHECK(cbuild_build(ctx, "after_reset") == 0, "the new graph did not build");
    CHECK(cbuild_build(ctx, "before_reset") != 0, "the old graph survived the reset");
    CHECK(cbuild_clean(ctx) == 0, "the new graph did not clean");
    cbuild_context_free(ctx);
}

SCENARIO(api, sections) {
    cbuild_context_t *ctx = new_capturing_context(capture_sections);
    CHECK(cbuild_dummy_target(ctx, "section_target") != NULL, "no target");
    CHECK(cbuild_build(ctx, NULL) == 0, "the build failed");
    CHECK(cbuild_clean(ctx) == 0, "the clean failed");
    CHECK(strcmp(capture.text, "Build\nClean\n") == 0, "sections were:\n%s", capture.text);
    cbuild_context_free(ctx);
}

static void expect_target_listing(const char *flag, int target_count) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    for (int i = 0; i < target_count; ++i) {
        const char *name = format_text("listing_target_%04d", i);
        const char *output = format_text("build/generated_%04d.h", i);
        CHECK((i % 2 ? cbuild_file_dep_target(ctx, name, output) : cbuild_dummy_target(ctx, name)) != NULL, "no target");
    }
    const char *args[] = { "cbuild", flag };
    CHECK(cbuild_run(ctx, 2, (char **)args) == 0, "%s failed", flag);
    CHECK(strstr(capture.text, "Available targets:\n") == capture.text, "no heading:\n%s", capture.text);
    for (int i = 0; i < target_count; ++i) {
        const char *name = format_text("listing_target_%04d", i);
        const char *expected = i % 2 ? format_text("  %-20s  [file_dep]  -> build/generated_%04d.h\n", name, i)
                                     : format_text("  %-20s  [dummy]\n", name);
        CHECK(has_text(capture.text, expected), "%s is missing '%s'", flag, expected);
    }
    cbuild_context_free(ctx);
}

SCENARIO(api, target_listing) {
    expect_target_listing("--list", 0);
    expect_target_listing("-l", 0);
    expect_target_listing("--list", 512);
    expect_target_listing("-l", 512);
}

SCENARIO(api, test_registration) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    const char *args[] = { "program" };
    CHECK(cbuild_add_test(NULL, "name", NULL, args, 1) == NULL, "no context was accepted");
    CHECK(cbuild_add_test(ctx, NULL, NULL, args, 1) == NULL, "no name was accepted");
    CHECK(cbuild_get_last_error(ctx) != NULL, "no last error");
    CHECK(cbuild_add_test(ctx, "", NULL, args, 1) == NULL, "an empty name was accepted");
    CHECK(cbuild_add_test(ctx, "negative", NULL, args, -1) == NULL, "a negative count was accepted");
    CHECK(cbuild_add_test(ctx, "missing args", NULL, NULL, 1) == NULL, "missing args were accepted");
    CHECK(cbuild_add_test(ctx, "no program", NULL, NULL, 0) == NULL, "a test without a program was accepted");

    target_t *phony = cbuild_dummy_target(ctx, "phony");
    CHECK(phony != NULL, "no target");
    CHECK(cbuild_add_test(ctx, "not runnable", phony, NULL, 0) == NULL, "a dummy target is not a program");
    CHECK(has_text(cbuild_get_last_error(ctx), "not runnable"), "the error does not name the test");

    cbuild_test_add_dependency(ctx, NULL, phony);
    cbuild_test_add_label(ctx, NULL, "label");
    cbuild_test_set_skip_code(ctx, NULL, 1);
    cbuild_test_set_timeout(ctx, NULL, 1);
    cbuild_test_set_lock(ctx, NULL, "lock");

    test_t *test = cbuild_add_test(ctx, "registered", NULL, args, 1);
    CHECK(test != NULL, "a valid test was rejected");
    cbuild_test_add_dependency(ctx, test, NULL);
    cbuild_test_add_label(ctx, test, NULL);
    cbuild_test_add_label(ctx, test, "");

    const char *list_args[] = { "cbuild", "--list-tests" };
    CHECK(cbuild_run(ctx, 2, (char **)list_args) == 0, "--list-tests failed");
    CHECK(has_text(capture.text, "Available tests:\n  registered\n"), "listing was:\n%s", capture.text);
    cbuild_context_free(ctx);
}

SCENARIO(api, spawn) {
    cbuild_context_t *ctx = cbuild_context_new();
    CHECK(ctx != NULL, "no context");
    const char *self = h_self();
    char *output = NULL;

    const char *both[] = { self, "--child", "4", "spawn out", "spawn err" };
    CHECK(cbuild_spawn(ctx, both, 5, &output) == 4, "wrong exit status");
    CHECK(output && has_text(output, "spawn out") && has_text(output, "spawn err"), "output was not captured");
    free(output);

    const char *silent[] = { self, "--child", "5" };
    output = (char *)both;
    CHECK(cbuild_spawn(ctx, silent, 3, &output) == 5, "wrong exit status");
    CHECK(output == NULL, "silent programs capture nothing");
    CHECK(cbuild_spawn(ctx, silent, 3, NULL) == 5, "wrong exit status without capture");

    const char *missing[] = { "no-such-program-for-cbuild-tests" };
    int rc = cbuild_spawn(ctx, missing, 1, &output);
    CHECK(rc == -1 || rc == 127, "a missing program reported %d", rc);
    free(output);

    const char *null_program[] = { NULL };
    output = (char *)both;
    CHECK(cbuild_spawn(ctx, null_program, 1, &output) == -1, "a NULL program was accepted");
    CHECK(output == NULL, "output was not cleared");
    CHECK(cbuild_spawn(NULL, silent, 3, NULL) == -1, "no context was accepted");
    CHECK(cbuild_spawn(ctx, NULL, 3, NULL) == -1, "no argv was accepted");
    CHECK(cbuild_spawn(ctx, silent, 0, NULL) == -1, "an empty argv was accepted");
    cbuild_context_free(ctx);
}

static void expect_run_tests(cbuild_context_t *ctx, const char *label, const char *pattern, int result, const char *expected) {
    clear_capture();
    int actual = cbuild_run_tests(ctx, label, pattern);
    if (actual != result) h_fail("cbuild_run_tests returned %d, expected %d:\n%s", actual, result, capture.text);
    if (!has_text(capture.text, expected)) h_fail("expected '%s' in:\n%s", expected, capture.text);
}

#define EXPECT_RUN_TESTS(ctx, label, pattern, result, expected) \
    H_AT(expect_run_tests((ctx), (label), (pattern), (result), (expected)))

SCENARIO(api, run_tests) {
    cbuild_context_t *ctx = new_capturing_context(capture_all);
    cbuild_set_parallelism(ctx, 2);
    CHECK(cbuild_run_tests(NULL, NULL, NULL) == -1, "no context was accepted");

    const char *self = h_self();
    const char *pass_args[] = { self, "--child", "0" };
    const char *skip_args[] = { self, "--child", "9" };
    const char *fail_args[] = { self, "--child", "3" };
    test_t *pass = cbuild_add_test(ctx, "run_pass", NULL, pass_args, 3);
    test_t *skip = cbuild_add_test(ctx, "run_skip", NULL, skip_args, 3);
    test_t *fail = cbuild_add_test(ctx, "run_fail", NULL, fail_args, 3);
    cbuild_test_add_label(ctx, pass, "good");
    cbuild_test_add_label(ctx, skip, "good");
    cbuild_test_add_label(ctx, fail, "bad");
    cbuild_test_set_skip_code(ctx, skip, 9);

    int built = 0;
    target_t *needed = cbuild_dummy_target(ctx, "needed");
    CHECK(needed != NULL, "no target");
    cbuild_target_add_command(ctx, needed, cbuild_command_function(ctx, "bump", count_callback, &built));
    cbuild_test_add_label(ctx, cbuild_add_test(ctx, "run_with_target", needed, pass_args, 3), "needs_target");

    char *failing_argv[] = { (char *)self, (char *)"--child", (char *)"1" };
    target_t *unbuildable = cbuild_dummy_target(ctx, "unbuildable");
    CHECK(unbuildable != NULL, "no target");
    cbuild_target_add_command(ctx, unbuildable, cbuild_command_argv(ctx, "fail", failing_argv, 3));
    cbuild_test_add_label(ctx, cbuild_add_test(ctx, "run_blocked", unbuildable, pass_args, 3), "blocked");

    EXPECT_RUN_TESTS(ctx, "good", NULL, 0, "Tests: 1 passed, 0 failed, 1 skipped");
    CHECK(built == 0, "an unselected test's target was built");

    EXPECT_RUN_TESTS(ctx, "bad", NULL, -1, "Tests: 0 passed, 1 failed, 0 skipped");
    EXPECT_FILE("out/tests/run_fail.log");

    EXPECT_RUN_TESTS(ctx, NULL, "run_pa*", 0, "Tests: 1 passed, 0 failed, 0 skipped");
    EXPECT_RUN_TESTS(ctx, "good", "run_skip", 0, "Tests: 0 passed, 0 failed, 1 skipped");

    EXPECT_RUN_TESTS(ctx, "needs_target", NULL, 0, "Tests: 1 passed, 0 failed, 0 skipped");
    CHECK(built == 1, "the selected test's target was built %d times", built);

    EXPECT_RUN_TESTS(ctx, "blocked", NULL, -1, "Build failed; tests not run.");
    CHECK(!has_text(capture.text, "run_blocked"), "a test ran after a failed build:\n%s", capture.text);

    EXPECT_RUN_TESTS(ctx, "no-such-label", NULL, 0, "no tests selected");
    EXPECT_RUN_TESTS(ctx, "", "run_*_target", 0, "Tests: 1 passed, 0 failed, 0 skipped");
    EXPECT_RUN_TESTS(ctx, "bad", "run_pass", 0, "no tests selected");

    cbuild_reset(ctx);
    cbuild_set_logger(ctx, capture_all, NULL);
    EXPECT_RUN_TESTS(ctx, NULL, NULL, 0, "no tests selected");
    cbuild_context_free(ctx);
}
