/* By default this exercises the shipped single header. The suite also builds
   it with CBUILD_TEST_SPLIT_SOURCES and links the separately compiled src/ files. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef CBUILD_TEST_SPLIT_SOURCES
#include "../src/cbuild.h"
#else
#define CBUILD_IMPLEMENTATION
#include "../cbuild.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int callback_count;
static int log_count;

static void count_callback(void *user_data) {
    int *value = (int *)user_data;
    (*value)++;
    callback_count++;
}

static void count_logger(void *user_data, cbuild_log_level_t level,
                         const char *message) {
    (void)level;
    (void)message;
    int *value = (int *)user_data;
    (*value)++;
    log_count++;
}

typedef struct {
    char text[65536];
    size_t length;
} listing_t;

static void capture_listing(void *user_data, cbuild_log_level_t level,
                            const char *message) {
    listing_t *listing = (listing_t *)user_data;
    size_t length = strlen(message);
    assert(level == CBUILD_LOG_INFO);
    assert(listing->length + length + 2 <= sizeof(listing->text));
    memcpy(listing->text + listing->length, message, length);
    listing->length += length;
    listing->text[listing->length++] = '\n';
    listing->text[listing->length] = '\0';
}

static void test_target_listing(const char *flag, int target_count) {
    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);
    listing_t listing = { { 0 }, 0 };
    cbuild_set_logger(ctx, capture_listing, &listing);
    for (int i = 0; i < target_count; ++i) {
        char name[64], output[128];
        snprintf(name, sizeof(name), "listing_target_%04d", i);
        snprintf(output, sizeof(output), "build/generated_%04d.h", i);
        assert((i % 2 ? cbuild_file_dep_target(ctx, name, output)
                      : cbuild_dummy_target(ctx, name)) != NULL);
    }
    const char *args[] = { "cbuild", flag };
    assert(cbuild_run(ctx, 2, (char **)args) == 0);
    assert(strstr(listing.text, "Available targets:\n") == listing.text);
    for (int i = 0; i < target_count; ++i) {
        char name[64], expected[256];
        snprintf(name, sizeof(name), "listing_target_%04d", i);
        if (i % 2) {
            snprintf(expected, sizeof(expected), "  %-20s  [file_dep]  -> build/generated_%04d.h\n", name, i);
        } else {
            snprintf(expected, sizeof(expected), "  %-20s  [dummy]\n", name);
        }
        assert(strstr(listing.text, expected) != NULL);
    }
    cbuild_context_free(ctx);
}

static void capture_log(void *user_data, cbuild_log_level_t level,
                        const char *message) {
    listing_t *log = (listing_t *)user_data;
    size_t length = strlen(message);
    (void)level;
    assert(log->length + length + 2 <= sizeof(log->text));
    memcpy(log->text + log->length, message, length);
    log->length += length;
    log->text[log->length++] = '\n';
    log->text[log->length] = '\0';
}

static void clear_log(listing_t *log) {
    log->length = 0;
    log->text[0] = '\0';
}

static void bump_counter(void *user_data) {
    (*(int *)user_data)++;
}

/* Exits with args[0] after printing args[1] to stdout and args[2] to stderr when present. */
static int run_child(int argc, char **args) {
    if (argc > 1) printf("%s\n", args[1]);
    if (argc > 2) fprintf(stderr, "%s\n", args[2]);
    return atoi(args[0]);
}

static void capture_sections(void *user_data, cbuild_log_level_t level,
                             const char *message) {
    if (level == CBUILD_LOG_SECTION) capture_log(user_data, level, message);
    assert(message[0] != '\0');
}

static void test_sections(void) {
    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);
    listing_t log = { { 0 }, 0 };
    cbuild_set_logger(ctx, capture_sections, &log);
    cbuild_set_output_dir(ctx, "tests/api-build");
    assert(cbuild_dummy_target(ctx, "section_target") != NULL);

    assert(cbuild_build(ctx, NULL) == 0);
    assert(cbuild_clean(ctx) == 0);
    assert(strcmp(log.text, "Build\nClean\n") == 0);

    cbuild_context_free(ctx);
}

static void test_flag_values(void) {
    const char *repeated[] = { "cbuild", "--mode=first", "--mode", "last" };
    assert(strcmp(cbuild_get_flag_value(4, (char **)repeated, "mode", 0), "last") == 0);

    const char *mixed[] = { "cbuild", "-j2", "--jobs=3" };
    assert(strcmp(cbuild_get_flag_value(3, (char **)mixed, "jobs", 'j'), "3") == 0);
    assert(strcmp(cbuild_get_flag_value(2, (char **)mixed, "jobs", 'j'), "2") == 0);
    assert(cbuild_get_flag_value(3, (char **)mixed, NULL, 'x') == NULL);
    assert(strcmp(cbuild_get_flag_value(3, (char **)mixed, NULL, 'j'), "2") == 0);

    const char *prefixes[] = { "cbuild", "--mode-extra=x", "--modes", "y" };
    assert(cbuild_get_flag_value(4, (char **)prefixes, "mode", 0) == NULL);

    const char *dangling[] = { "cbuild", "--mode=kept", "--mode" };
    assert(strcmp(cbuild_get_flag_value(3, (char **)dangling, "mode", 0), "kept") == 0);
    assert(cbuild_get_flag_value(2, (char **)dangling + 1, "mode", 0) == NULL);

    const char *empty[] = { "cbuild", "--mode=" };
    assert(strcmp(cbuild_get_flag_value(2, (char **)empty, "mode", 0), "") == 0);

    const char *program_name[] = { "--mode=program" };
    assert(cbuild_get_flag_value(1, (char **)program_name, "mode", 0) == NULL);
}

static void test_test_registration(void) {
    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);
    listing_t log = { { 0 }, 0 };
    cbuild_set_logger(ctx, capture_log, &log);
    cbuild_set_output_dir(ctx, "tests/api-build");

    const char *args[] = { "program" };
    assert(cbuild_add_test(NULL, "name", NULL, args, 1) == NULL);
    assert(cbuild_add_test(ctx, NULL, NULL, args, 1) == NULL);
    assert(cbuild_get_last_error(ctx) != NULL);
    assert(cbuild_add_test(ctx, "", NULL, args, 1) == NULL);
    assert(cbuild_add_test(ctx, "negative", NULL, args, -1) == NULL);
    assert(cbuild_add_test(ctx, "missing args", NULL, NULL, 1) == NULL);

    target_t *phony = cbuild_dummy_target(ctx, "phony");
    assert(phony != NULL);
    assert(cbuild_add_test(ctx, "not runnable", phony, NULL, 0) == NULL);
    assert(strstr(cbuild_get_last_error(ctx), "not runnable") != NULL);

    cbuild_test_add_dependency(ctx, NULL, phony);
    cbuild_test_add_label(ctx, NULL, "label");
    cbuild_test_set_skip_code(ctx, NULL, 1);
    cbuild_test_set_timeout(ctx, NULL, 1);
    cbuild_test_set_lock(ctx, NULL, "lock");

    test_t *test = cbuild_add_test(ctx, "registered", NULL, args, 1);
    assert(test != NULL);
    cbuild_test_add_dependency(ctx, test, NULL);
    cbuild_test_add_label(ctx, test, NULL);
    cbuild_test_add_label(ctx, test, "");

    const char *list_args[] = { "cbuild", "--list-tests" };
    assert(cbuild_run(ctx, 2, (char **)list_args) == 0);
    assert(strstr(log.text, "Available tests:\n  registered\n") != NULL);

    cbuild_context_free(ctx);
}

static void test_spawn(const char *self) {
    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);
    char *output = NULL;

    const char *both[] = { self, "--child", "4", "spawn out", "spawn err" };
    assert(cbuild_spawn(ctx, both, 5, &output) == 4);
    assert(output != NULL);
    assert(strstr(output, "spawn out") != NULL);
    assert(strstr(output, "spawn err") != NULL);
    free(output);

    const char *silent[] = { self, "--child", "5" };
    output = (char *)both;
    assert(cbuild_spawn(ctx, silent, 3, &output) == 5);
    assert(output == NULL);
    assert(cbuild_spawn(ctx, silent, 3, NULL) == 5);

    const char *missing[] = { "no-such-program-for-cbuild-tests" };
    int rc = cbuild_spawn(ctx, missing, 1, &output);
    assert(rc == -1 || rc == 127);
    free(output);

    const char *null_program[] = { NULL };
    output = (char *)both;
    assert(cbuild_spawn(ctx, null_program, 1, &output) == -1);
    assert(output == NULL);
    assert(cbuild_spawn(NULL, silent, 3, NULL) == -1);
    assert(cbuild_spawn(ctx, NULL, 3, NULL) == -1);
    assert(cbuild_spawn(ctx, silent, 0, NULL) == -1);

    cbuild_context_free(ctx);
}

static void test_run_tests(const char *self) {
    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);
    listing_t log = { { 0 }, 0 };
    cbuild_set_logger(ctx, capture_log, &log);
    cbuild_set_output_dir(ctx, "tests/api-build");
    cbuild_set_parallelism(ctx, 2);

    assert(cbuild_run_tests(NULL, NULL, NULL) == -1);

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
    assert(needed != NULL);
    cbuild_target_add_command(ctx, needed, cbuild_command_function(ctx, "bump", bump_counter, &built));
    test_t *with_target = cbuild_add_test(ctx, "run_with_target", needed, pass_args, 3);
    cbuild_test_add_label(ctx, with_target, "needs_target");

    char *failing_argv[] = { (char *)self, (char *)"--child", (char *)"1" };
    target_t *unbuildable = cbuild_dummy_target(ctx, "unbuildable");
    assert(unbuildable != NULL);
    cbuild_target_add_command(ctx, unbuildable, cbuild_command_argv(ctx, "fail", failing_argv, 3));
    test_t *blocked = cbuild_add_test(ctx, "run_blocked", unbuildable, pass_args, 3);
    cbuild_test_add_label(ctx, blocked, "blocked");

    assert(cbuild_run_tests(ctx, "good", NULL) == 0);
    assert(strstr(log.text, "Tests: 1 passed, 0 failed, 1 skipped") != NULL);
    assert(built == 0);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "bad", NULL) == -1);
    assert(strstr(log.text, "Tests: 0 passed, 1 failed, 0 skipped") != NULL);
    assert(cbuild_file_exists("tests/api-build/tests/run_fail.log"));

    clear_log(&log);
    assert(cbuild_run_tests(ctx, NULL, "run_pa*") == 0);
    assert(strstr(log.text, "Tests: 1 passed, 0 failed, 0 skipped") != NULL);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "good", "run_skip") == 0);
    assert(strstr(log.text, "Tests: 0 passed, 0 failed, 1 skipped") != NULL);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "needs_target", NULL) == 0);
    assert(strstr(log.text, "Tests: 1 passed, 0 failed, 0 skipped") != NULL);
    assert(built == 1);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "blocked", NULL) == -1);
    assert(strstr(log.text, "Build failed; tests not run.") != NULL);
    assert(strstr(log.text, "run_blocked") == NULL);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "no-such-label", NULL) == 0);
    assert(strstr(log.text, "no tests selected") != NULL);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "", "run_*_target") == 0);
    assert(strstr(log.text, "Tests: 1 passed, 0 failed, 0 skipped") != NULL);

    clear_log(&log);
    assert(cbuild_run_tests(ctx, "bad", "run_pass") == 0);
    assert(strstr(log.text, "no tests selected") != NULL);

    cbuild_reset(ctx);
    cbuild_set_logger(ctx, capture_log, &log);
    clear_log(&log);
    assert(cbuild_run_tests(ctx, NULL, NULL) == 0);
    assert(strstr(log.text, "no tests selected") != NULL);

    cbuild_context_free(ctx);
}

static void make_dir(const char *path) {
#ifdef _WIN32
    assert(_mkdir(path) == 0);
#else
    assert(mkdir(path, 0777) == 0);
#endif
}

static void make_file(const char *path) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs("contents\n", file);
    fclose(file);
}

/* target is relative to the working directory. */
static void make_dir_link(const char *link, const char *target) {
    char cwd[1024], absolute[2048];
    assert(cbuild_get_cwd(cwd, sizeof(cwd)) == 0);
    snprintf(absolute, sizeof(absolute), "%s/%s", cwd, target);
#ifdef _WIN32
    char command[4096];
    snprintf(command, sizeof(command), "mklink /J \"%s\" \"%s\" >NUL", link, absolute);
    for (char *p = command; *p; ++p) {
        if (*p == '/' && p > command + 8) *p = '\\';
    }
    assert(system(command) == 0);
#else
    assert(symlink(absolute, link) == 0);
#endif
}

static void test_remove_dir(void) {
    make_dir("tests/api-remove");
    make_dir("tests/api-remove/precious");
    make_file("tests/api-remove/precious/keep.txt");
    make_dir("tests/api-remove/tree");
    make_dir("tests/api-remove/tree/nested");
    make_file("tests/api-remove/tree/nested/file.txt");
    make_dir_link("tests/api-remove/tree/link", "tests/api-remove/precious");
    make_dir_link("tests/api-remove/tree/nested/deep", "tests/api-remove/precious");
    make_dir_link("tests/api-remove/top", "tests/api-remove/precious");
#ifndef _WIN32
    assert(symlink("no-such-target", "tests/api-remove/tree/dangling") == 0);
#endif

    assert(cbuild_remove_dir("tests/api-remove/top") == 0);
    assert(!cbuild_dir_exists("tests/api-remove/top"));
    assert(cbuild_file_exists("tests/api-remove/precious/keep.txt"));

    assert(cbuild_remove_dir("tests/api-remove/tree") == 0);
    assert(!cbuild_dir_exists("tests/api-remove/tree"));
    assert(cbuild_file_exists("tests/api-remove/precious/keep.txt"));

    assert(cbuild_remove_dir("tests/api-remove/no-such-dir") == 0);
    assert(cbuild_remove_dir(NULL) == -1);
    assert(cbuild_remove_dir("") == -1);

    assert(cbuild_remove_dir("tests/api-remove") == 0);
    assert(!cbuild_dir_exists("tests/api-remove"));
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--child") == 0) return run_child(argc - 2, argv + 2);

    test_remove_dir();
    test_sections();
    test_flag_values();
    test_test_registration();
    test_spawn(argv[0]);
    test_run_tests(argv[0]);

    test_target_listing("--list", 0);
    test_target_listing("-l", 0);
    test_target_listing("--list", 512);
    test_target_listing("-l", 512);
    assert(strcmp(CBUILD_VERSION, "v0.1.2") == 0);
    assert(CBUILD_VERSION_MAJOR == 0);
    assert(CBUILD_VERSION_MINOR == 1);
    assert(CBUILD_VERSION_PATCH == 2);

    cbuild_context_t *ctx = cbuild_context_new();
    assert(ctx != NULL);

    int user_value = 42;
    cbuild_set_user_data(ctx, &user_value);
    assert(cbuild_get_user_data(ctx) == &user_value);

    int logger_calls = 0;
    cbuild_set_logger(ctx, count_logger, &logger_calls);

    assert(cbuild_file_exists("cbuild.h"));
    assert(cbuild_dir_exists("example"));
    assert(cbuild_match_wildcard("*.c", "build.c"));
    assert(!cbuild_match_wildcard("*.h", "build.c"));

    char **files = NULL;
    int file_count = 0;
    assert(cbuild_expand_wildcard("example/lib/src/*.c", &files,
                                  &file_count) == 0);
    assert(file_count == 2);
    for (int i = 0; i < file_count; ++i) free(files[i]);
    free(files);

    assert(cbuild_expand_wildcard("example/**/*.c", &files, &file_count) == 0);
    assert(file_count == 6);
    assert(strcmp(files[0], "example/build.c") == 0);
    assert(strcmp(files[1], "example/lib/build.c") == 0);
    assert(strcmp(files[2], "example/lib/src/lib.c") == 0);
    assert(strcmp(files[3], "example/lib/src/math.c") == 0);
    assert(strcmp(files[4], "example/main.c") == 0);
    assert(strcmp(files[5], "example/tests/test_math.c") == 0);
    for (int i = 0; i < file_count; ++i) free(files[i]);
    free(files);
    assert(cbuild_expand_wildcard("example/*/src/*.h", &files, &file_count) == 0);
    assert(file_count == 2);
    assert(strcmp(files[0], "example/lib/src/lib.h") == 0);
    for (int i = 0; i < file_count; ++i) free(files[i]);
    free(files);

    const char *flag_argv[] = { "cbuild", "--mode=fast", "-j", "4", "--out", "dir", "--", "--mode=late" };
    assert(strcmp(cbuild_get_flag_value(8, (char **)flag_argv, "mode", 0), "fast") == 0);
    assert(strcmp(cbuild_get_flag_value(8, (char **)flag_argv, "jobs", 'j'), "4") == 0);
    assert(strcmp(cbuild_get_flag_value(8, (char **)flag_argv, "out", 0), "dir") == 0);
    assert(cbuild_get_flag_value(8, (char **)flag_argv, "missing", 0) == NULL);
    assert(cbuild_add_test(ctx, "no program", NULL, NULL, 0) == NULL);

    FILE *temporary = fopen("tests/api-temp.txt", "w");
    assert(temporary != NULL);
    fputs("temporary\n", temporary);
    fclose(temporary);
    assert(cbuild_file_exists("tests/api-temp.txt"));
    assert(cbuild_remove_file("tests/api-temp.txt") == 0);
    assert(!cbuild_file_exists("tests/api-temp.txt"));

    int command_calls = 0;
    command_t *callback = cbuild_command_function(
        ctx, "callback smoke test", count_callback, &command_calls);
    assert(callback != NULL);
    assert(cbuild_run_command(ctx, callback) == 0);
    assert(command_calls == 1);
    assert(callback_count == 1);

    config_t *config = cbuild_config_new(ctx, "Smoke");
    assert(config != NULL);
    cbuild_config_add_cflags(ctx, config, "-DAPI_SMOKE");
    cbuild_config_add_ldflags(ctx, config, "-s");
    cbuild_config_add_define(ctx, config, "SMOKE=1");
    cbuild_config_add_include(ctx, config, "example/lib/src");
    cbuild_config_add_libdir(ctx, config, "tests");
    cbuild_config_add_linklib(ctx, config, "m");
    cbuild_config_set_opt(ctx, config, 1);
    cbuild_config_set_debug(ctx, config, 1);
    cbuild_config_set_lto(ctx, config, 0);
    cbuild_config_set_pic(ctx, config, 1);
    cbuild_config_set_warnings(ctx, config, 1);
    cbuild_config_set_std(ctx, config, "c11");
    cbuild_config_set_runtime(ctx, config, "dynamic");
    cbuild_config_set_output_dir(ctx, config, "tests/api-build");
    cbuild_config_set_freestanding(ctx, config, 0);
    cbuild_config_enable_sanitizers(ctx, config, 1);
    cbuild_config_disable_sanitizers(ctx, config, 1);
    cbuild_set_active_config(ctx, config);
    assert(cbuild_config_default_debug(ctx) != NULL);
    assert(cbuild_config_default_release(ctx) != NULL);
    assert(cbuild_config_default_wasm32(ctx) != NULL);

    target_t *phony = cbuild_dummy_target(ctx, "phony");
    assert(phony != NULL);
    cbuild_target_set_config(ctx, phony, config);
    assert(cbuild_build(ctx, "phony") == 0);

    assert(cbuild_add_source(ctx, NULL, "missing.c") == -1);
    assert(cbuild_get_last_error(ctx) != NULL);

    assert(logger_calls > 0);
    assert(log_count > 0);

    cbuild_reset(ctx);
    cbuild_set_output_dir(ctx, "tests/api-build");
    assert(cbuild_dummy_target(ctx, "after_reset") != NULL);
    assert(cbuild_build(ctx, "after_reset") == 0);
    assert(cbuild_clean(ctx) == 0);

    cbuild_context_free(ctx);
    return 0;
}
