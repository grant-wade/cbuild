#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

static const char *output_argument(int argc, char **argv) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) return argv[i + 1];
    }
    return NULL;
}

static int write_unstartable_program(const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 1;
    fputs("this is not a program\n", file);
    if (fclose(file) != 0) return 1;
#ifndef _WIN32
    chmod(path, 0755);
#endif
    return 0;
}

static int forward_to_real_compiler(int argc, char **argv) {
    names_t command = split_flags(getenv("FAKE_COMPILER_REAL"));
    for (int i = 0; i < argc; ++i) names_add(&command, argv[i]);
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;
    int status = cbuild_spawn(ctx, command.items, command.count, NULL);
    cbuild_context_free(ctx);
    return status;
}

int fake_compiler(int argc, char **argv) {
    FILE *record = fopen("compilers-used.txt", "ab");
    if (!record) return 1;
    fprintf(record, "%s\n", argv[0]);
    fclose(record);

    const char *mode = argv[1];
    if (strcmp(mode, "fail") == 0) return 1;
    if (strcmp(mode, "unstartable") == 0) {
        const char *output = output_argument(argc - 2, argv + 2);
        return output ? write_unstartable_program(output) : 1;
    }
    if (strcmp(mode, "forward") == 0) return forward_to_real_compiler(argc - 2, argv + 2);
    return 1;
}
