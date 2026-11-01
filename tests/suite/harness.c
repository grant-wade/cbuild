#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

static void **owned;
static size_t owned_count;
static size_t owned_capacity;

static cbuild_context_t *spawn_context;
static const char *repository_root;
static const char *self_path;
static const char *scratch_path;
static const char *build_program;
static const char *last_log;
static const char *fail_file = "?";
static int fail_line;

void *h_alloc(size_t size) {
    if (owned_count == owned_capacity) {
        owned_capacity = owned_capacity ? owned_capacity * 2 : 256;
        owned = (void **)realloc(owned, owned_capacity * sizeof(void *));
        if (!owned) abort();
    }
    void *block = malloc(size ? size : 1);
    if (!block) abort();
    owned[owned_count++] = block;
    return block;
}

void h_at(const char *file, int line) {
    fail_file = file;
    fail_line = line;
}

void h_fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("%s:%d: ", fail_file, fail_line);
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
    exit(1);
}

void h_skip(const char *reason) {
    printf("skipped: %s\n", reason);
    fflush(stdout);
    exit(SCENARIO_SKIP_EXIT);
}

static const char *vformat_text(const char *format, va_list args) {
    va_list measure;
    va_copy(measure, args);
    int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0) abort();
    char *text = (char *)h_alloc((size_t)length + 1);
    vsnprintf(text, (size_t)length + 1, format, args);
    return text;
}

const char *format_text(const char *format, ...) {
    va_list args;
    va_start(args, format);
    const char *text = vformat_text(format, args);
    va_end(args);
    return text;
}

const char *replace_text(const char *text, const char *old_text, const char *new_text) {
    size_t old_length = strlen(old_text);
    size_t new_length = strlen(new_text);
    char *out = (char *)h_alloc(strlen(text) * (new_length + 1) + 1);
    char *end = out;
    while (*text) {
        if (strncmp(text, old_text, old_length) == 0) {
            memcpy(end, new_text, new_length);
            end += new_length;
            text += old_length;
        } else {
            *end++ = *text++;
        }
    }
    *end = '\0';
    return out;
}

int has_text(const char *text, const char *needle) {
    return strstr(text, needle) != NULL;
}

int count_text(const char *text, const char *needle) {
    int count = 0;
    size_t length = strlen(needle);
    for (const char *at = strstr(text, needle); at; at = strstr(at + length, needle)) count++;
    return count;
}

int glob_match(const char *pattern, const char *text) {
    for (; *pattern; ++pattern, ++text) {
        if (*pattern == '*') {
            while (pattern[1] == '*') pattern++;
            if (!pattern[1]) return 1;
            for (; *text; ++text) {
                if (glob_match(pattern + 1, text)) return 1;
            }
            return 0;
        }
        if (!*text || (*pattern != '?' && *pattern != *text)) return 0;
    }
    return !*text;
}

static const char *line_copy(const char *start, size_t length) {
    char *line = (char *)h_alloc(length + 1);
    memcpy(line, start, length);
    line[length] = '\0';
    return line;
}

const char *find_line(const char *text, const char *pattern) {
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        const char *line = line_copy(text, length);
        if (glob_match(pattern, line)) return line;
        text += length + (end ? 1 : 0);
    }
    return NULL;
}

int count_lines(const char *text, const char *pattern) {
    int count = 0;
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (glob_match(pattern, line_copy(text, length))) count++;
        text += length + (end ? 1 : 0);
    }
    return count;
}

int count_steps(const char *text, const char *label, const char *mention) {
    return count_lines(text, format_text("%s *%s*", label, mention ? mention : ""));
}

void names_add(names_t *names, const char *item) {
    if (names->count == names->capacity) {
        int capacity = names->capacity ? names->capacity * 2 : 16;
        const char **items = (const char **)h_alloc(sizeof(char *) * (size_t)capacity);
        for (int i = 0; i < names->count; ++i) items[i] = names->items[i];
        names->items = items;
        names->capacity = capacity;
    }
    names->items[names->count++] = item;
}

static int compare_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void names_sort(names_t *names) {
    if (names->count > 0) qsort((void *)names->items, (size_t)names->count, sizeof(char *), compare_names);
}

names_t split_flags(const char *flags) {
    names_t names = { NULL, 0, 0 };
    const char *at = flags;
    while (at && *at) {
        while (*at == ' ' || *at == '\t') at++;
        if (!*at) break;
        char *token = (char *)h_alloc(strlen(at) + 1);
        size_t length = 0;
        int quoted = 0;
        for (; *at && (quoted || (*at != ' ' && *at != '\t')); ++at) {
            if (*at == '"') {
                quoted = !quoted;
            } else {
                token[length++] = *at;
            }
        }
        token[length] = '\0';
        names_add(&names, token);
    }
    return names;
}

void h_expect_names(names_t actual, int count, const char *const *expected) {
    int same = actual.count == count;
    for (int i = 0; same && i < count; ++i) same = strcmp(actual.items[i], expected[i]) == 0;
    if (same) return;
    printf("expected:\n");
    for (int i = 0; i < count; ++i) printf("  %s\n", expected[i]);
    printf("actual:\n");
    for (int i = 0; i < actual.count; ++i) printf("  %s\n", actual.items[i]);
    h_fail("lists differ");
}

static void print_if_not_shown(const char *text) {
    if (text != last_log) printf("in:\n%s\n", text);
}

void h_expect_has(const char *text, const char *needle) {
    if (has_text(text, needle)) return;
    print_if_not_shown(text);
    h_fail("expected to find '%s'", needle);
}

void h_expect_lacks(const char *text, const char *needle) {
    if (!has_text(text, needle)) return;
    print_if_not_shown(text);
    h_fail("did not expect to find '%s'", needle);
}

void h_expect_line(const char *text, const char *pattern) {
    if (find_line(text, pattern)) return;
    print_if_not_shown(text);
    h_fail("expected a line matching '%s'", pattern);
}

void h_expect_no_line(const char *text, const char *pattern) {
    const char *line = find_line(text, pattern);
    if (!line) return;
    print_if_not_shown(text);
    h_fail("line '%s' should not match '%s'", line, pattern);
}

void h_expect_steps(const char *text, const char *label, int count, const char *mention) {
    int actual = count_steps(text, label, mention);
    if (actual == count) return;
    print_if_not_shown(text);
    h_fail("expected %d %s step(s) mentioning '%s', saw %d", count, label, mention ? mention : "", actual);
}

void h_expect_order(const char *text, int count, const char *const *needles) {
    const char *at = text;
    for (int i = 0; i < count; ++i) {
        const char *found = strstr(at, needles[i]);
        if (!found) {
            print_if_not_shown(text);
            h_fail("expected '%s' after '%s'", needles[i], i ? needles[i - 1] : "the start");
        }
        at = found + strlen(needles[i]);
    }
}

void h_expect_file(const char *path) {
    if (!is_file(path)) h_fail("expected the file %s", path);
}

void h_expect_gone(const char *path) {
    if (path_exists(path)) h_fail("expected %s not to exist", path);
}

void set_env(const char *name, const char *value) {
#ifdef _WIN32
    if (_putenv_s(name, value) != 0) h_fail("could not set %s", name);
#else
    if (setenv(name, value, 1) != 0) h_fail("could not set %s", name);
#endif
}

void unset_env(const char *name) {
#ifdef _WIN32
    if (_putenv_s(name, "") != 0) h_fail("could not unset %s", name);
#else
    if (unsetenv(name) != 0) h_fail("could not unset %s", name);
#endif
}

const char *with_inherited(const char *name, const char *flags) {
    const char *inherited = getenv(name);
    return inherited && *inherited ? format_text("%s %s", inherited, flags) : flags;
}

#ifdef _WIN32
static HANDLE original_stdin;
static HANDLE fed_stdin;
#endif

static void release_fed_stdin(void) {
#ifdef _WIN32
    if (!fed_stdin) return;
    SetStdHandle(STD_INPUT_HANDLE, original_stdin);
    CloseHandle(fed_stdin);
    fed_stdin = NULL;
#endif
}

void feed_stdin(const char *text) {
    release_fed_stdin();
    write_text("stdin.txt", text);
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    original_stdin = GetStdHandle(STD_INPUT_HANDLE);
    fed_stdin = CreateFileA("stdin.txt", GENERIC_READ, FILE_SHARE_READ, &attributes, OPEN_EXISTING, 0, NULL);
    if (fed_stdin == INVALID_HANDLE_VALUE || !SetStdHandle(STD_INPUT_HANDLE, fed_stdin)) h_fail("could not replace stdin");
#else
    if (!freopen("stdin.txt", "r", stdin)) h_fail("could not replace stdin");
#endif
}

static const char *without_carriage_returns(const char *text, size_t length) {
    char *out = (char *)h_alloc(length + 1);
    size_t written = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n') continue;
        out[written++] = text[i];
    }
    out[written] = '\0';
    return out;
}

const char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) h_fail("could not read %s", path);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *raw = (char *)h_alloc((size_t)size + 1);
    size_t got = fread(raw, 1, (size_t)size, file);
    fclose(file);
    return without_carriage_returns(raw, got);
}

static void write_mode(const char *path, const char *text, const char *mode) {
    FILE *file = fopen(path, mode);
    if (!file) h_fail("could not write %s", path);
    fwrite(text, 1, strlen(text), file);
    if (fclose(file) != 0) h_fail("could not finish writing %s", path);
}

void write_text(const char *path, const char *text) {
    write_mode(path, text, "wb");
}

void append_text(const char *path, const char *text) {
    write_mode(path, text, "ab");
}

void replace_in_file(const char *path, const char *old_text, const char *new_text) {
    const char *text = read_text(path);
    if (!has_text(text, old_text)) h_fail("%s does not contain '%s'", path, old_text);
    write_text(path, replace_text(text, old_text, new_text));
}

void touch_file(const char *path) {
    write_text(path, read_text(path));
}

int is_file(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

int is_dir(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

int path_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return lstat(path, &info) == 0;
#endif
}

static int try_make_dir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

void make_dir(const char *path) {
    if (try_make_dir(path) != 0) h_fail("could not create the directory %s", path);
}

void make_dirs(const char *path) {
    char *partial = (char *)h_alloc(strlen(path) + 1);
    strcpy(partial, path);
    for (char *at = partial + 1; *at; ++at) {
        if ((*at != '/' && *at != '\\') || at[-1] == ':') continue;
        char saved = *at;
        *at = '\0';
        try_make_dir(partial);
        *at = saved;
    }
    try_make_dir(partial);
    if (!is_dir(partial)) h_fail("could not create the directory %s", path);
}

void delete_file(const char *path) {
    if (remove(path) != 0) h_fail("could not delete %s", path);
}

void delete_tree(const char *path) {
    enum { ATTEMPTS = 50, RETRY_MS = 100 };
    for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
        if (cbuild_remove_dir(path) == 0) return;
        sleep_ms(RETRY_MS);
    }
    h_fail("could not delete the directory %s", path);
}

void rename_path(const char *from, const char *to) {
    if (rename(from, to) != 0) h_fail("could not rename %s to %s", from, to);
}

void copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) h_fail("could not read %s", from);
    FILE *out = fopen(to, "wb");
    if (!out) h_fail("could not write %s", to);
    char buffer[8192];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) h_fail("could not write %s", to);
    }
    fclose(in);
    if (fclose(out) != 0) h_fail("could not finish writing %s", to);
}

static names_t list_dir(const char *directory) {
    names_t names = { NULL, 0, 0 };
#ifdef _WIN32
    WIN32_FIND_DATAA entry;
    HANDLE find = FindFirstFileA(format_text("%s/*", directory), &entry);
    if (find == INVALID_HANDLE_VALUE) h_fail("could not list %s", directory);
    do {
        if (strcmp(entry.cFileName, ".") != 0 && strcmp(entry.cFileName, "..") != 0) {
            names_add(&names, format_text("%s", entry.cFileName));
        }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
#else
    DIR *dir = opendir(directory);
    if (!dir) h_fail("could not list %s", directory);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            names_add(&names, format_text("%s", entry->d_name));
        }
    }
    closedir(dir);
#endif
    names_sort(&names);
    return names;
}

static int is_local_build_output(const char *name) {
    static const char *const patterns[] = { "build", "cbuild", "cbuild.exe", "*.old", "*.o", "*.obj", "*.d", "*.sig",
                                            "*.log", "*.dSYM", "compile_commands.json", ".cbuild-nested-report" };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        if (glob_match(patterns[i], name)) return 1;
    }
    return 0;
}

void copy_tree(const char *from, const char *to) {
    if (!is_dir(to)) make_dirs(to);
    names_t names = list_dir(from);
    for (int i = 0; i < names.count; ++i) {
        if (is_local_build_output(names.items[i])) continue;
        const char *source = format_text("%s/%s", from, names.items[i]);
        const char *destination = format_text("%s/%s", to, names.items[i]);
        if (is_dir(source)) {
            copy_tree(source, destination);
        } else {
            copy_file(source, destination);
        }
    }
}

void change_dir(const char *path) {
#ifdef _WIN32
    int rc = _chdir(path);
#else
    int rc = chdir(path);
#endif
    if (rc != 0) h_fail("could not enter %s", path);
}

const char *current_dir(void) {
    char *buffer = (char *)h_alloc(4096);
    if (cbuild_get_cwd(buffer, 4096) != 0) h_fail("could not read the working directory");
    return buffer;
}

names_t files_with_suffix(const char *directory, const char *suffix) {
    names_t all = list_dir(directory);
    names_t found = { NULL, 0, 0 };
    size_t suffix_length = strlen(suffix);
    for (int i = 0; i < all.count; ++i) {
        size_t length = strlen(all.items[i]);
        if (length >= suffix_length && strcmp(all.items[i] + length - suffix_length, suffix) == 0) {
            names_add(&found, format_text("%s/%s", directory, all.items[i]));
        }
    }
    if (found.count == 0) h_fail("no %s files in %s", suffix, directory);
    return found;
}

void link_dir(const char *link, const char *target) {
    const char *absolute = format_text("%s/%s", current_dir(), target);
#ifdef _WIN32
    const char *command = format_text("mklink /J \"%s\" \"%s\" >NUL", replace_text(link, "/", "\\"),
                                      replace_text(absolute, "/", "\\"));
    if (system(command) != 0) h_fail("could not create the junction %s", link);
#else
    if (symlink(absolute, link) != 0) h_fail("could not create the link %s", link);
#endif
}

void link_file(const char *link, const char *target) {
#ifdef _WIN32
    (void)target;
    h_fail("could not create the link %s: file links need privileges on Windows", link);
#else
    if (symlink(format_text("%s/%s", current_dir(), target), link) != 0) h_fail("could not create the link %s", link);
#endif
}

void remove_dir_link(const char *link) {
#ifdef _WIN32
    int rc = _rmdir(link);
#else
    int rc = unlink(link);
#endif
    if (rc != 0) h_fail("could not remove the link %s", link);
}

void sleep_ms(int milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec duration;
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&duration, NULL);
#endif
}

void settle(void) {
    sleep_ms(100);
}

double now_seconds(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

void use_program(const char *path) {
    build_program = path;
}

const char *prebuilt_program(const char *name) {
    return root_path(format_text("build/%s%s", name, EXE));
}

void compile_build_program(void) {
    names_t command = split_flags(getenv("CC"));
#ifdef _WIN32
    static const char *const flags[] = { "/nologo", "/std:c11", "/W4", "/D_CRT_SECURE_NO_WARNINGS", "build.c", "/Fe:cbuild.exe" };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) names_add(&command, flags[i]);
#else
    const char *cflags = getenv("CFLAGS");
    names_t flags = split_flags(cflags && *cflags ? cflags : "-std=c11 -Wall -Wextra -Wpedantic -Werror");
    for (int i = 0; i < flags.count; ++i) names_add(&command, flags.items[i]);
    names_add(&command, "build.c");
    names_add(&command, "-o");
    names_add(&command, "cbuild");
    names_t ldflags = split_flags(getenv("LDFLAGS"));
    for (int i = 0; i < ldflags.count; ++i) names_add(&command, ldflags.items[i]);
#endif
    outcome_t outcome = h_run_names(command);
    if (outcome.status != 0) h_fail("could not compile the build program");
    use_program(format_text("%s/cbuild%s", current_dir(), EXE));
}

outcome_t h_run(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)argc);
    for (int i = 0; i < argc; ++i) argv[i] = args[i];
#ifdef _WIN32
    argv[0] = replace_text(argv[0], "/", "\\");
#endif
    printf("$");
    for (int i = 0; i < argc; ++i) printf(strchr(argv[i], ' ') || !*argv[i] ? " '%s'" : " %s", argv[i]);
    printf("\n");
    fflush(stdout);

    char *raw = NULL;
    int status = cbuild_spawn(spawn_context, argv, argc, &raw);
    outcome_t outcome;
    outcome.status = status;
    outcome.log = without_carriage_returns(raw ? raw : "", raw ? strlen(raw) : 0);
    free(raw);
    last_log = outcome.log;
    printf("%s", outcome.log);
    if (*outcome.log && outcome.log[strlen(outcome.log) - 1] != '\n') printf("\n");
    printf("[exit status %d]\n", status);
    fflush(stdout);
    return outcome;
}

outcome_t h_run_names(names_t command) {
    return h_run(command.count, command.items);
}

outcome_t h_cbuild(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)(argc + 1));
    argv[0] = build_program;
    for (int i = 0; i < argc; ++i) argv[i + 1] = args[i];
    return h_run(argc + 1, argv);
}

const char *h_build(int argc, const char *const *args) {
    outcome_t outcome = h_cbuild(argc, args);
    if (outcome.status != 0) h_fail("the build program exited with status %d", outcome.status);
    return outcome.log;
}

const char *h_build_fails(int argc, const char *const *args) {
    outcome_t outcome = h_cbuild(argc, args);
    if (outcome.status == 0) h_fail("the build program succeeded but had to fail");
    return outcome.log;
}

#ifndef _WIN32
int background_start(int argc, const char *const *args) {
    const char **argv = (const char **)h_alloc(sizeof(char *) * (size_t)(argc + 1));
    for (int i = 0; i < argc; ++i) argv[i] = args[i];
    argv[argc] = NULL;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &actions, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) h_fail("could not start %s", argv[0]);
    return (int)pid;
}

int background_is_running(int pid) {
    int status;
    return waitpid((pid_t)pid, &status, WNOHANG) == 0;
}

int background_stop(int pid, int sig) {
    kill((pid_t)pid, sig);
    int status = 0;
    if (waitpid((pid_t)pid, &status, 0) != (pid_t)pid) h_fail("could not wait for process %d", pid);
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
}
#endif

void h_init(const char *self, const char *scratch) {
    spawn_context = cbuild_context_new();
    if (!spawn_context) abort();
    repository_root = current_dir();
    int absolute = self[0] == '/' || self[0] == '\\' || (self[0] && self[1] == ':');
    self_path = absolute ? self : format_text("%s/%s", repository_root, self);

    const char *compiler = getenv("CC");
    if (!compiler || !*compiler) {
#ifdef _WIN32
        set_env("CC", "cl");
#else
        set_env("CC", "cc");
#endif
    }
    set_env("NO_COLOR", "1");
    unset_env("CLICOLOR_FORCE");
    unset_env("CBUILD_NESTED");

    scratch_path = root_path(format_text("build/test work/%s", scratch));
    delete_tree(scratch_path);
    make_dirs(scratch_path);
    change_dir(scratch_path);
}

void h_finish(void) {
    release_fed_stdin();
    change_dir(repository_root);
    delete_tree(scratch_path);
    cbuild_context_free(spawn_context);
    spawn_context = NULL;
}

const char *h_self(void) {
    return self_path;
}

const char *root_path(const char *relative) {
    return format_text("%s/%s", repository_root, relative);
}
