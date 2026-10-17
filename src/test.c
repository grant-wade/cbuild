/* test.c - test registration and the parallel test runner. */

#include "cbuild_internal.h"

enum { CBUILD_TEST_PENDING, CBUILD_TEST_RUNNING, CBUILD_TEST_DONE };
enum { CBUILD_TEST_PASS, CBUILD_TEST_FAIL, CBUILD_TEST_SKIP };

test_t* cbuild_add_test(cbuild_context_t* ctx, const char* name, target_t* target,
                        const char* const* args, int argc) {
    if (!ctx || !name || !*name || argc < 0 || (argc > 0 && !args)) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_test");
        return NULL;
    }
    if (!(target && target->type == TARGET_EXECUTABLE) && argc == 0) {
        cbuild__set_error(ctx, "Test '%s' needs an executable target or a program in args[0]", name);
        return NULL;
    }
    test_t* test = (test_t*)calloc(1, sizeof(test_t));
    if (!test) {
        cbuild__set_error(ctx, "Out of memory in cbuild_add_test");
        return NULL;
    }
    test->name = strdup(name);
    test->target = target;
    test->skip_code = -1;
    test->argc = argc;
    test->args = (char**)calloc((size_t)argc + 1, sizeof(char*));
    for (int i = 0; i < argc; ++i) test->args[i] = strdup(args[i]);
    ensure_capacity_charpp(ctx, (char***)&ctx->tests, &ctx->test_count, &ctx->test_cap);
    ctx->tests[ctx->test_count++] = test;
    return test;
}

void cbuild_test_add_dependency(cbuild_context_t* ctx, test_t* test, target_t* target) {
    if (!test || !target) return;
    ensure_capacity_charpp(ctx, (char***)&test->deps, &test->dep_count, &test->dep_cap);
    test->deps[test->dep_count++] = target;
}

void cbuild_test_add_label(cbuild_context_t* ctx, test_t* test, const char* label) {
    if (!test || !label || !*label) return;
    ensure_capacity_charpp(ctx, &test->labels, &test->label_count, &test->label_cap);
    test->labels[test->label_count++] = strdup(label);
}

void cbuild_test_set_skip_code(cbuild_context_t* ctx, test_t* test, int exit_code) {
    (void)ctx;
    if (test) test->skip_code = exit_code;
}

void cbuild_test_set_timeout(cbuild_context_t* ctx, test_t* test, int seconds) {
    (void)ctx;
    if (test) test->timeout = seconds > 0 ? seconds : 0;
}

void cbuild_test_set_lock(cbuild_context_t* ctx, test_t* test, const char* lock) {
    (void)ctx;
    if (!test) return;
    free(test->lock);
    test->lock = (lock && *lock) ? strdup(lock) : NULL;
}

CBUILD_INTERNAL void cbuild__free_tests(cbuild_context_t* ctx) {
    for (int i = 0; i < ctx->test_count; ++i) {
        test_t* test = ctx->tests[i];
        if (!test) continue;
        free(test->name);
        for (int j = 0; j < test->argc; ++j) free(test->args[j]);
        free(test->args);
        free(test->deps);
        for (int j = 0; j < test->label_count; ++j) free(test->labels[j]);
        free(test->labels);
        free(test->lock);
        free(test);
    }
    free(ctx->tests);
    ctx->tests = NULL;
    ctx->test_count = 0;
    ctx->test_cap = 0;

    char*** lists[] = { &ctx->test_labels, &ctx->test_filters, &ctx->test_excludes };
    int* counts[] = { &ctx->test_label_count, &ctx->test_filter_count, &ctx->test_exclude_count };
    int* caps[] = { &ctx->test_label_cap, &ctx->test_filter_cap, &ctx->test_exclude_cap };
    for (int l = 0; l < 3; ++l) {
        for (int i = 0; i < *counts[l]; ++i) free((*lists[l])[i]);
        free(*lists[l]);
        *lists[l] = NULL;
        *counts[l] = 0;
        *caps[l] = 0;
    }
    ctx->run_tests = 0;
    ctx->list_tests = 0;
    ctx->test_timeout = 0;
}

/* Selected when it has one of the requested labels (if any), matches one of
 * the name filters (if any), and matches no exclusion. */
static int cbuild__test_selected(cbuild_context_t* ctx, test_t* test) {
    if (ctx->test_label_count > 0) {
        int found = 0;
        for (int i = 0; i < ctx->test_label_count && !found; ++i) {
            for (int j = 0; j < test->label_count; ++j) {
                if (strcmp(ctx->test_labels[i], test->labels[j]) == 0) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) return 0;
    }
    if (ctx->test_filter_count > 0) {
        int found = 0;
        for (int i = 0; i < ctx->test_filter_count; ++i) {
            if (cbuild_match_wildcard(ctx->test_filters[i], test->name)) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    for (int i = 0; i < ctx->test_exclude_count; ++i) {
        if (cbuild_match_wildcard(ctx->test_excludes[i], test->name)) return 0;
    }
    return 1;
}

static const char* cbuild__test_program(test_t* test) {
    if (test->target && test->target->type == TARGET_EXECUTABLE) return test->target->output_file;
    return test->args[0];
}

static int cbuild__test_program_is_target(test_t* test) {
    return test->target && test->target->type == TARGET_EXECUTABLE;
}

CBUILD_INTERNAL void cbuild__list_tests(cbuild_context_t* ctx) {
    int shown = 0;
    cbuild__log(ctx, CBUILD_LOG_INFO, "Available tests:");
    for (int i = 0; i < ctx->test_count; ++i) {
        test_t* test = ctx->tests[i];
        if (!cbuild__test_selected(ctx, test)) continue;
        char* labels = NULL;
        for (int j = 0; j < test->label_count; ++j) {
            append_format(&labels, "%s%s", j ? "," : "", test->labels[j]);
        }
        if (labels) {
            cbuild__log(ctx, CBUILD_LOG_INFO, "  %-40s  [%s]", test->name, labels);
        } else {
            cbuild__log(ctx, CBUILD_LOG_INFO, "  %s", test->name);
        }
        free(labels);
        shown++;
    }
    if (shown == 0) cbuild__log(ctx, CBUILD_LOG_INFO, "  (no tests)");
}

/* ---------------- Running one test ---------------- */

static double cbuild__now_seconds(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static void cbuild__sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

typedef struct {
    cbuild_context_t* ctx;
    test_t** tests;
    int* state;
    int* result;
    int count;
    int started;
    int finished;
    int passed, failed, skipped;
    char* log_dir;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    CRITICAL_SECTION spawn_mutex;
#else
    pthread_mutex_t mutex;
#endif
} cbuild_test_run_t;

static void cbuild__test_run_lock(cbuild_test_run_t* run) {
#ifdef _WIN32
    EnterCriticalSection(&run->mutex);
#else
    pthread_mutex_lock(&run->mutex);
#endif
}

static void cbuild__test_run_unlock(cbuild_test_run_t* run) {
#ifdef _WIN32
    LeaveCriticalSection(&run->mutex);
#else
    pthread_mutex_unlock(&run->mutex);
#endif
}

/* Runs argv with stdin from the null device and stdout and stderr in log_path.
 * Returns the exit status, -1 if it could not be started, and sets *timed_out
 * when the process was killed for exceeding timeout seconds. */
static int cbuild__spawn_logged(cbuild_test_run_t* run, cbuild_argv_t* argv, const char* log_path,
                                int timeout, int* timed_out) {
    *timed_out = 0;
#ifdef _WIN32
    /* Handles marked inheritable leak into any process created meanwhile, so
     * they only exist inside this critical section. */
    EnterCriticalSection(&run->spawn_mutex);
    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, 0, NULL);
    if (out == INVALID_HANDLE_VALUE || in == INVALID_HANDLE_VALUE) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        LeaveCriticalSection(&run->spawn_mutex);
        return -1;
    }
    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi = { 0 };
    char* cmdline = cbuild_argv_to_cmdline(argv);
    BOOL ok = cmdline && CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);
    CloseHandle(out);
    CloseHandle(in);
    LeaveCriticalSection(&run->spawn_mutex);
    if (!ok) return -1;

    DWORD wait = WaitForSingleObject(pi.hProcess, timeout > 0 ? (DWORD)timeout * 1000 : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        *timed_out = 1;
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    (void)run;
    /* O_CLOEXEC keeps these out of processes other workers start meanwhile;
     * dup2 in the child clears it on the standard descriptors. */
    int out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (out < 0 || in < 0) {
        if (out >= 0) close(out);
        if (in >= 0) close(in);
        return -1;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out, STDERR_FILENO);
    pid_t pid;
    int spawn_rc = posix_spawnp(&pid, argv->args[0], &actions, NULL, argv->args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out);
    close(in);
    if (spawn_rc != 0) return -1;

    double deadline = timeout > 0 ? cbuild__now_seconds() + timeout : 0;
    int delay_ms = 1;
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, timeout > 0 ? WNOHANG : 0);
        if (r == pid) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (cbuild__now_seconds() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            *timed_out = 1;
            break;
        }
        cbuild__sleep_ms(delay_ms);
        if (delay_ms < 50) delay_ms *= 2;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

static char* cbuild__read_text_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = size >= 0 ? (char*)malloc((size_t)size + 1) : NULL;
    if (text) {
        size_t n = fread(text, 1, (size_t)size, f);
        text[n] = '\0';
    }
    fclose(f);
    return text;
}

static void cbuild__run_one_test(cbuild_test_run_t* run, int index) {
    cbuild_context_t* ctx = run->ctx;
    test_t* test = run->tests[index];

    char* log_path = NULL;
    append_format(&log_path, "%s/", run->log_dir);
    for (const char* p = test->name; *p; ++p) {
        char c = (isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_') ? *p : '_';
        append_format(&log_path, "%c", c);
    }
    append_format(&log_path, ".log");

    cbuild_argv_t argv;
    cbuild_argv_init(&argv);
    char* program = strdup(cbuild__test_program(test));
#ifdef _WIN32
    /* CreateProcess resolves the program from the command line, which wants native separators. */
    for (char* p = program; p && *p; ++p) {
        if (*p == '/') *p = '\\';
    }
#endif
    cbuild_argv_append(&argv, program);
    free(program);
    for (int i = cbuild__test_program_is_target(test) ? 0 : 1; i < test->argc; ++i) {
        cbuild_argv_append(&argv, test->args[i]);
    }

    int timeout = test->timeout > 0 ? test->timeout : ctx->test_timeout;
    int timed_out = 0;
    double start = cbuild__now_seconds();
    int rc = cbuild__spawn_logged(run, &argv, log_path, timeout, &timed_out);
    double elapsed = cbuild__now_seconds() - start;
    cbuild_argv_free(&argv);

    int result = CBUILD_TEST_FAIL;
    char detail[128] = "";
    if (timed_out) {
        snprintf(detail, sizeof(detail), ", timed out after %ds", timeout);
    } else if (rc == 0) {
        result = CBUILD_TEST_PASS;
    } else if (test->skip_code >= 0 && rc == test->skip_code) {
        result = CBUILD_TEST_SKIP;
    } else if (rc < 0) {
        snprintf(detail, sizeof(detail), ", could not start %s", cbuild__test_program(test));
    } else {
        snprintf(detail, sizeof(detail), ", exit code %d", rc);
    }

    cbuild__test_run_lock(run);
    run->result[index] = result;
    run->state[index] = CBUILD_TEST_DONE;
    run->finished++;
    if (result == CBUILD_TEST_PASS) run->passed++;
    if (result == CBUILD_TEST_FAIL) run->failed++;
    if (result == CBUILD_TEST_SKIP) run->skipped++;
    const char* label = result == CBUILD_TEST_PASS ? "PASS" : result == CBUILD_TEST_SKIP ? "SKIP" : "FAIL";
    cbuild__log_step(ctx, label, CBUILD_COLOR_BLUE, "[%d/%d] %s (%.2fs%s)", run->finished, run->count,
                     test->name, elapsed, detail);
    if (result == CBUILD_TEST_FAIL) {
        char* text = cbuild__read_text_file(log_path);
        if (text && *text) {
            fwrite(text, 1, strlen(text), stderr);
            if (text[strlen(text) - 1] != '\n') fputc('\n', stderr);
            fflush(stderr);
        }
        free(text);
    }
    cbuild__test_run_unlock(run);
    free(log_path);
}

/* Next pending test whose lock no running test holds, or -1. Caller holds the mutex. */
static int cbuild__next_runnable_test(cbuild_test_run_t* run) {
    for (int i = 0; i < run->count; ++i) {
        if (run->state[i] != CBUILD_TEST_PENDING) continue;
        const char* lock = run->tests[i]->lock;
        int blocked = 0;
        for (int j = 0; lock && j < run->count && !blocked; ++j) {
            if (run->state[j] == CBUILD_TEST_RUNNING && run->tests[j]->lock &&
                strcmp(run->tests[j]->lock, lock) == 0) {
                blocked = 1;
            }
        }
        if (!blocked) return i;
    }
    return -1;
}

#ifdef _WIN32
static DWORD WINAPI cbuild__test_worker(void* arg) {
#else
static void* cbuild__test_worker(void* arg) {
#endif
    cbuild_test_run_t* run = (cbuild_test_run_t*)arg;
    for (;;) {
        cbuild__test_run_lock(run);
        if (run->started >= run->count) {
            cbuild__test_run_unlock(run);
            break;
        }
        int index = cbuild__next_runnable_test(run);
        if (index >= 0) {
            run->state[index] = CBUILD_TEST_RUNNING;
            run->started++;
        }
        cbuild__test_run_unlock(run);
        if (index < 0) {
            cbuild__sleep_ms(5);  /* everything left waits on a lock */
            continue;
        }
        cbuild__run_one_test(run, index);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* Builds what the selected tests need, then runs them. Returns 0 when nothing failed. */
CBUILD_INTERNAL int cbuild__run_selected_tests(cbuild_context_t* ctx) {
    cbuild_test_run_t run;
    memset(&run, 0, sizeof(run));
    run.ctx = ctx;
    run.tests = (test_t**)calloc((size_t)ctx->test_count + 1, sizeof(test_t*));
    for (int i = 0; i < ctx->test_count; ++i) {
        if (cbuild__test_selected(ctx, ctx->tests[i])) run.tests[run.count++] = ctx->tests[i];
    }
    if (run.count == 0) {
        cbuild__log(ctx, CBUILD_LOG_WARNING, "cbuild: no tests selected");
        free(run.tests);
        return 0;
    }

    /* Build first: a test can only start once everything it needs exists. */
    int error_flag = 0;
    free(ctx->visited);
    free(ctx->in_stack);
    ctx->visited = (int*)calloc((size_t)ctx->target_count + 1, sizeof(int));
    ctx->in_stack = (int*)calloc((size_t)ctx->target_count + 1, sizeof(int));
    for (int i = 0; i < run.count && !error_flag; ++i) {
        if (run.tests[i]->target) dfs_build_func(ctx, run.tests[i]->target, &error_flag);
        for (int j = 0; j < run.tests[i]->dep_count && !error_flag; ++j) {
            dfs_build_func(ctx, run.tests[i]->deps[j], &error_flag);
        }
    }
    free(ctx->visited);
    ctx->visited = NULL;
    free(ctx->in_stack);
    ctx->in_stack = NULL;
    if (error_flag) {
        cbuild__log_status(ctx, 0, "Build failed; tests not run.");
        free(run.tests);
        return -1;
    }

    const char* output_dir = ctx->active_config && ctx->active_config->output_dir
                                 ? ctx->active_config->output_dir
                                 : ctx->output_dir;
    append_format(&run.log_dir, "%s/tests", output_dir);
    ensure_dir_exists(run.log_dir);

    run.state = (int*)calloc((size_t)run.count, sizeof(int));
    run.result = (int*)calloc((size_t)run.count, sizeof(int));
    int thread_count = ctx->parallel_jobs < run.count ? ctx->parallel_jobs : run.count;
    if (thread_count < 1) thread_count = 1;
#ifdef _WIN32
    /* WaitForMultipleObjects joins at most this many threads. */
    if (thread_count > MAXIMUM_WAIT_OBJECTS) thread_count = MAXIMUM_WAIT_OBJECTS;
#endif

    cbuild__log_step(ctx, "TEST", CBUILD_COLOR_BLUE, "Running %d test%s with %d job%s", run.count,
                     run.count == 1 ? "" : "s", thread_count, thread_count == 1 ? "" : "s");
    double start = cbuild__now_seconds();

#ifdef _WIN32
    InitializeCriticalSection(&run.mutex);
    InitializeCriticalSection(&run.spawn_mutex);
    HANDLE* threads = (HANDLE*)malloc(sizeof(HANDLE) * thread_count);
    for (int i = 0; i < thread_count; ++i) {
        threads[i] = CreateThread(NULL, 0, cbuild__test_worker, &run, 0, NULL);
    }
    WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE);
    for (int i = 0; i < thread_count; ++i) CloseHandle(threads[i]);
    DeleteCriticalSection(&run.spawn_mutex);
    DeleteCriticalSection(&run.mutex);
#else
    pthread_mutex_init(&run.mutex, NULL);
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
    for (int i = 0; i < thread_count; ++i) {
        pthread_create(&threads[i], NULL, cbuild__test_worker, &run);
    }
    for (int i = 0; i < thread_count; ++i) pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&run.mutex);
#endif
    free(threads);

    double elapsed = cbuild__now_seconds() - start;
    if (run.failed > 0) {
        cbuild__log(ctx, CBUILD_LOG_INFO, "Failed tests (logs in %s):", run.log_dir);
        for (int i = 0; i < run.count; ++i) {
            if (run.result[i] == CBUILD_TEST_FAIL) cbuild__log(ctx, CBUILD_LOG_INFO, "  %s", run.tests[i]->name);
        }
    }
    cbuild__log_status(ctx, run.failed == 0, "Tests: %d passed, %d failed, %d skipped (%.1fs)", run.passed,
                       run.failed, run.skipped, elapsed);

    int failed = run.failed;
    free(run.state);
    free(run.result);
    free(run.tests);
    free(run.log_dir);
    return failed ? -1 : 0;
}

int cbuild_run_tests(cbuild_context_t* ctx, const char* label, const char* pattern) {
    if (!ctx) return -1;
    cbuild_init(ctx);
    cbuild__resolve_target_paths(ctx);

    /* Narrow the selection for this call only. */
    int added_label = 0, added_filter = 0;
    if (label && *label) {
        ensure_capacity_charpp(ctx, &ctx->test_labels, &ctx->test_label_count, &ctx->test_label_cap);
        ctx->test_labels[ctx->test_label_count++] = strdup(label);
        added_label = 1;
    }
    if (pattern && *pattern) {
        ensure_capacity_charpp(ctx, &ctx->test_filters, &ctx->test_filter_count, &ctx->test_filter_cap);
        ctx->test_filters[ctx->test_filter_count++] = strdup(pattern);
        added_filter = 1;
    }
    int rc = cbuild__run_selected_tests(ctx);
    if (added_label) free(ctx->test_labels[--ctx->test_label_count]);
    if (added_filter) free(ctx->test_filters[--ctx->test_filter_count]);
    return rc;
}
