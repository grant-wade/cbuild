/* harness.h - helpers shared by the test scenarios. Strings they return live until the process exits. */
#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>

#include "cbuild.h"
#include "scenarios.h"

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

#define SECTION_BAR "\xE2\x94\x80"
#define STATUS_OK "\xE2\x9C\x94"
#define STATUS_FAIL "\xE2\x9C\x96"

/* Exit status of a scenario that does not apply on this platform. */
enum { SCENARIO_SKIP_EXIT = 77 };

/* Result of a finished program. */
typedef struct {
    int status;       /* exit status */
    const char *log;  /* stdout and stderr with \n line endings, never NULL */
} outcome_t;

/* A list of strings. */
typedef struct {
    const char **items;
    int count;
    int capacity;
} names_t;

#define SCENARIO(group, name) void group##_##name(void)

#define X(group, name) SCENARIO(group, name);
SCENARIOS(X)
#undef X

/* Stands in for a compiler. argv is NAME MODE COMPILER_ARGS..., where NAME is appended to
 * compilers-used.txt and MODE is fail, unstartable, or forward (to FAKE_COMPILER_REAL). */
int fake_compiler(int argc, char **argv);

void h_init(const char *self, const char *scratch);
void h_finish(void);
const char *h_self(void);
/* The path of a file in the repository. */
const char *root_path(const char *relative);

void h_at(const char *file, int line);
void h_fail(const char *format, ...);
void h_skip(const char *reason);
#define H_AT(expression) (h_at(__FILE__, __LINE__), (expression))
#define CHECK(condition, ...)                  \
    do {                                       \
        if (!(condition)) {                    \
            h_at(__FILE__, __LINE__);          \
            h_fail(__VA_ARGS__);               \
        }                                      \
    } while (0)

#define H_COUNT(...) ((int)(sizeof((const char *[]){ "", __VA_ARGS__ }) / sizeof(const char *)) - 1)
#define H_LIST(...) (((const char *[]){ "", __VA_ARGS__ }) + 1)
#define H_ARGS(...) H_COUNT(__VA_ARGS__), H_LIST(__VA_ARGS__)

void *h_alloc(size_t size);
const char *format_text(const char *format, ...);
const char *replace_text(const char *text, const char *old_text, const char *new_text);
int has_text(const char *text, const char *needle);
int count_text(const char *text, const char *needle);
int glob_match(const char *pattern, const char *text);
/* The first line of text matching a glob with * and ?, or NULL. */
const char *find_line(const char *text, const char *pattern);
int count_lines(const char *text, const char *pattern);
/* Lines of text that start with label followed by a space and contain mention, which may be NULL. */
int count_steps(const char *text, const char *label, const char *mention);

void names_add(names_t *names, const char *item);
void names_sort(names_t *names);
names_t split_flags(const char *flags);
void h_expect_names(names_t actual, int count, const char *const *expected);
#define EXPECT_NAMES(actual, ...) H_AT(h_expect_names((actual), H_ARGS(__VA_ARGS__)))
#define EXPECT_NO_NAMES(actual) H_AT(h_expect_names((actual), 0, NULL))

void h_expect_has(const char *text, const char *needle);
void h_expect_lacks(const char *text, const char *needle);
void h_expect_line(const char *text, const char *pattern);
void h_expect_no_line(const char *text, const char *pattern);
void h_expect_steps(const char *text, const char *label, int count, const char *mention);
void h_expect_order(const char *text, int count, const char *const *needles);
void h_expect_file(const char *path);
void h_expect_gone(const char *path);
#define EXPECT_HAS(text, needle) H_AT(h_expect_has((text), (needle)))
#define EXPECT_LACKS(text, needle) H_AT(h_expect_lacks((text), (needle)))
#define EXPECT_LINE(text, pattern) H_AT(h_expect_line((text), (pattern)))
#define EXPECT_NO_LINE(text, pattern) H_AT(h_expect_no_line((text), (pattern)))
#define EXPECT_STEPS(text, label, count, mention) H_AT(h_expect_steps((text), (label), (count), (mention)))
#define EXPECT_ORDER(text, ...) H_AT(h_expect_order((text), H_ARGS(__VA_ARGS__)))
#define EXPECT_FILE(path) H_AT(h_expect_file(path))
#define EXPECT_GONE(path) H_AT(h_expect_gone(path))

void set_env(const char *name, const char *value);
void unset_env(const char *name);
/* The inherited value of name followed by flags. */
const char *with_inherited(const char *name, const char *flags);
/* Makes text the standard input of programs started from now on. */
void feed_stdin(const char *text);

const char *read_text(const char *path);
void write_text(const char *path, const char *text);
void append_text(const char *path, const char *text);
void replace_in_file(const char *path, const char *old_text, const char *new_text);
/* Gives a file a new modification time at the file system's full resolution. */
void touch_file(const char *path);
int is_file(const char *path);
int is_dir(const char *path);
/* True for links too, including dangling ones. */
int path_exists(const char *path);
void make_dir(const char *path);
void make_dirs(const char *path);
void delete_file(const char *path);
void delete_tree(const char *path);
void rename_path(const char *from, const char *to);
void copy_file(const char *from, const char *to);
/* Skips build outputs in from. */
void copy_tree(const char *from, const char *to);
void change_dir(const char *path);
const char *current_dir(void);
names_t files_with_suffix(const char *directory, const char *suffix);
/* target is relative to the working directory. */
void link_dir(const char *link, const char *target);
void link_file(const char *link, const char *target);
void remove_dir_link(const char *link);

/* Waits past the coarsest clock tick file systems stamp files with. */
void settle(void);
void sleep_ms(int milliseconds);
double now_seconds(void);

/* Sets the build program that CBUILD and friends run. */
void use_program(const char *path);
const char *prebuilt_program(const char *name);
/* Compiles build.c in the working directory into ./cbuild and uses it. */
void compile_build_program(void);
outcome_t h_run(int argc, const char *const *args);
outcome_t h_run_names(names_t command);
outcome_t h_cbuild(int argc, const char *const *args);
const char *h_build(int argc, const char *const *args);
const char *h_build_fails(int argc, const char *const *args);
#define RUN(...) H_AT(h_run(H_ARGS(__VA_ARGS__)))
#define CBUILD(...) H_AT(h_cbuild(H_ARGS(__VA_ARGS__)))
#define BUILD(...) H_AT(h_build(H_ARGS(__VA_ARGS__)))
#define BUILD_FAILS(...) H_AT(h_build_fails(H_ARGS(__VA_ARGS__)))

#ifndef _WIN32
/* Starts args in the background with its output discarded. Returns its process id. */
int background_start(int argc, const char *const *args);
int background_is_running(int pid);
/* Sends sig to the process and waits. Returns the signal that ended it, or 0. */
int background_stop(int pid, int sig);
#endif

/* A parsed JSON value. */
typedef struct json {
    enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } type;
    const char *text;     /* JSON_STRING contents */
    struct json **items;  /* JSON_ARRAY elements or JSON_OBJECT values */
    const char **keys;    /* JSON_OBJECT keys */
    int count;
} json_t;

/* Returns NULL when text is not valid JSON. */
json_t *json_parse(const char *text);
/* The value of key in an object, or NULL. */
json_t *json_get(const json_t *object, const char *key);

#endif
