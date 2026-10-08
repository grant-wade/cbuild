/* By default this exercises the shipped single header. The suite also builds
   it with CBUILD_TEST_SPLIT_SOURCES and links the separately compiled src/ files. */
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

int main(void) {
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
