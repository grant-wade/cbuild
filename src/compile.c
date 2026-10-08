/* compile.c - compile argv, signatures, header dependencies, and the parallel job queue. */

#include "cbuild_internal.h"

/* Thread worker argument - passes context to worker threads */
typedef struct {
    cbuild_context_t* ctx;
} compile_worker_arg_t;

/* Build the compiler argv for one source file. The compile step, the rebuild
 * signature, and compile_commands.json all go through here so they cannot drift. */
static void cbuild__compile_argv(cbuild_context_t* ctx, target_t* t, const char* src_file,
                                 const char* obj_file, const char* dep_file, cbuild_argv_t* argv) {
    int msvc = cbuild_target_cc_kind(ctx, t) == CBUILD_CC_MSVC;
    const char* inc_prefix = msvc ? "/I" : "-I";
    const char* def_prefix = msvc ? "/D" : "-D";

    /* Use append_flags to handle compilers with spaces (e.g., "zig cc -target ...") */
    cbuild_argv_append_flags(argv, cbuild_target_compiler(ctx, t));

    if (msvc) {
        cbuild_argv_append(argv, "/c");
        cbuild_argv_append(argv, "/nologo");
        cbuild__argv_append_prefixed(argv, "/Fo", obj_file);
        cbuild_argv_append(argv, "/showIncludes");
    } else {
        cbuild_argv_append(argv, "-c");
        cbuild_argv_append(argv, "-o");
        cbuild_argv_append(argv, obj_file);
        /* Add dependency generation flags for GCC/Clang */
        if (dep_file) {
            cbuild_argv_append(argv, "-MMD");
            cbuild_argv_append(argv, "-MF");
            cbuild_argv_append(argv, dep_file);
        }
    }

    for (int i = 0; i < ctx->global_cflag_count; ++i) {
        cbuild_argv_append(argv, ctx->global_cflags[i]);
    }
    for (int i = 0; i < t->cflag_count; ++i) {
        cbuild_argv_append(argv, t->cflags[i]);
    }
    for (int i = 0; i < t->include_count; ++i) {
        cbuild__argv_append_prefixed(argv, inc_prefix, t->include_dirs[i]);
    }
    for (int i = 0; i < ctx->global_def_count; ++i) {
        cbuild__argv_append_prefixed(argv, def_prefix, ctx->global_defines[i]);
    }
    for (int i = 0; i < t->define_count; ++i) {
        cbuild__argv_append_prefixed(argv, def_prefix, t->defines[i]);
    }

    cbuild_argv_append(argv, src_file);
}

/* Everything that must stay the same for an object file to be reusable. */
static char* cbuild__compile_signature(cbuild_argv_t* argv) {
    char* sig = NULL;
    for (int i = 0; i < argv->count; ++i) {
        append_format(&sig, "%s\n", argv->args[i]);
    }
    const char* ev;
    ev = getenv("CFLAGS");
    if (ev) append_format(&sig, "ENV:CFLAGS=%s\n", ev);
    ev = getenv("CPPFLAGS");
    if (ev) append_format(&sig, "ENV:CPPFLAGS=%s\n", ev);
    return sig;
}

static int cbuild__is_line_continuation(const char* p) {
    return p[0] == '\\' && (p[1] == '\n' || (p[1] == '\r' && p[2] == '\n'));
}

/* Returns 1 when a make-style dependency file (GCC/Clang -MMD) is unreadable, or
 * names a prerequisite that is missing or newer than obj_mtime. Understands the
 * escapes compilers emit: "\ " and "\#" for literal characters, "$$" for "$". */
static int cbuild__make_deps_outdated(const char* dep_file, cbuild_mtime_t obj_mtime) {
    FILE* df = fopen(dep_file, "rb");
    if (!df) return 1;
    fseek(df, 0, SEEK_END);
    long fsize = ftell(df);
    fseek(df, 0, SEEK_SET);
    char* content = (fsize >= 0) ? (char*)malloc((size_t)fsize + 1) : NULL;
    if (!content) {
        fclose(df);
        return 1;
    }
    size_t read_size = fread(content, 1, (size_t)fsize, df);
    content[read_size] = '\0';
    fclose(df);

    /* Skip "target:". The separator colon is followed by whitespace or a line
     * continuation, which tells it apart from a Windows drive letter (C:\...). */
    char* p = content;
    while (*p && !(*p == ':' && (p[1] == '\0' || p[1] == ' ' || p[1] == '\t' || p[1] == '\n' ||
                                 p[1] == '\r' || cbuild__is_line_continuation(p + 1)))) {
        p++;
    }
    if (!*p) {
        free(content);
        return 1;
    }
    p++;

    int outdated = 0;
    while (!outdated) {
        for (;;) {
            if (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            } else if (cbuild__is_line_continuation(p)) {
                p += (p[1] == '\r') ? 3 : 2;
            } else {
                break;
            }
        }
        /* Only the first rule lists prerequisites; -MP adds phony rules after it. */
        if (*p == '\0' || *p == '\n') break;

        /* Unescape the path in place; the result is never longer than the input. */
        char* token = p;
        char* out = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' &&
               !cbuild__is_line_continuation(p)) {
            if (*p == '\\') {
                /* Make halves a run of backslashes before a space or '#'; an odd
                 * run makes that character literal. Elsewhere they stand as-is. */
                size_t run = 1;
                while (p[run] == '\\') run++;
                char next = p[run];
                if (next == ' ' || next == '\t' || next == '#') {
                    for (size_t k = 0; k < run / 2; ++k) *out++ = '\\';
                    p += run;
                    if (run % 2) *out++ = *p++;
                    continue;
                }
            } else if (*p == '$' && p[1] == '$') {
                p++;
            }
            *out++ = *p++;
        }
        char terminator = *p;
        *out = '\0';

        cbuild_mtime_t dep_mtime;
        if (cbuild__mtime(token, &dep_mtime) != 0 || dep_mtime > obj_mtime) outdated = 1;

        if (out == p) *p = terminator;
    }

    free(content);
    return outdated;
}

static int need_recompile(cbuild_context_t* ctx, const char* src_file, const char* obj_file,
                          const char* dep_file, target_t* t) {
    /* First: compute and compare a signature of the would-be compile command */
    char* sig = NULL;
    cbuild_cc_kind_t cc_kind = cbuild_target_cc_kind(ctx, t);
    {
        cbuild_argv_t argv;
        cbuild_argv_init(&argv);
        cbuild__compile_argv(ctx, t, src_file, obj_file, dep_file, &argv);
        sig = cbuild__compile_signature(&argv);
        cbuild_argv_free(&argv);
    }
    if (!sig) return 1;

    char sigpath[1024];
    snprintf(sigpath, sizeof(sigpath), "%s.sig", obj_file);
    int sig_mismatch = 0;
    {
        FILE* f = fopen(sigpath, "rb");
        if (!f) {
            sig_mismatch = 1;
#ifdef CBUILD_DEBUG_SIGNATURE
            fprintf(stderr, "DEBUG: Signature file missing for %s (%s)\n", src_file, sigpath);
#endif
        } else {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* prev = (char*)malloc((size_t)sz + 1);
            if (!prev) {
                sig_mismatch = 1;
            } else {
                size_t rd = fread(prev, 1, (size_t)sz, f);
                prev[rd] = '\0';
                if (strcmp(prev, sig) != 0) {
                    sig_mismatch = 1;
#ifdef CBUILD_DEBUG_SIGNATURE
                    fprintf(stderr, "DEBUG: Signature mismatch for %s\n", src_file);
                    fprintf(stderr, "--- PREV (from file, %ld bytes) ---\n%s\n", sz, prev);
                    fprintf(stderr, "--- NEW (computed, %zu bytes) ---\n%s\n", strlen(sig), sig);
                    fprintf(stderr, "--- END ---\n");
#endif
                }
                free(prev);
            }
            fclose(f);
        }
    }
    if (sig) free(sig);
    if (sig_mismatch) {
        return 1;
    }

    /* Fallback: timestamp checks on src/object and header dependencies */
    cbuild_mtime_t src_mtime, obj_mtime;
    if (cbuild__mtime(src_file, &src_mtime) != 0) {
#ifdef CBUILD_DEBUG_SIGNATURE
        fprintf(stderr, "DEBUG: stat() failed for source file: %s\n", src_file);
#endif
        return 1;
    }
    if (cbuild__mtime(obj_file, &obj_mtime) != 0) {
#ifdef CBUILD_DEBUG_SIGNATURE
        fprintf(stderr, "DEBUG: stat() failed for object file: %s\n", obj_file);
#endif
        return 1;
    }
    if (src_mtime > obj_mtime) {
#ifdef CBUILD_DEBUG_SIGNATURE
        fprintf(stderr, "DEBUG: Source newer than object: %s (src=%llu, obj=%llu)\n",
                src_file, src_mtime, obj_mtime);
#endif
        return 1;
    }

    /* Check header dependencies from .d file */
    if (dep_file) {
        FILE* df = fopen(dep_file, "r");
        if (!df) return 1;

        /* MSVC uses a cbuild-specific, one-path-per-line format so paths
         * containing spaces (such as Windows SDK paths) remain intact. */
        if (cc_kind == CBUILD_CC_MSVC) {
            char path[4096];
            if (!fgets(path, sizeof(path), df)) {
                fclose(df);
                return 1;
            }
            path[strcspn(path, "\r\n")] = '\0';
            if (strcmp(path, "CBUILD_MSVC_DEPS_V1") != 0) {
                fclose(df);
                return 1;
            }

            while (fgets(path, sizeof(path), df)) {
                path[strcspn(path, "\r\n")] = '\0';
                if (!path[0]) continue;

                cbuild_mtime_t dep_mtime;
                if (cbuild__mtime(path, &dep_mtime) != 0 || dep_mtime > obj_mtime) {
#ifdef CBUILD_DEBUG_SIGNATURE
                    fprintf(stderr, "DEBUG: MSVC dependency changed or missing for %s: %s\n",
                            src_file, path);
#endif
                    fclose(df);
                    return 1;
                }
            }
            fclose(df);
            return 0;
        }

        fclose(df);
        if (cbuild__make_deps_outdated(dep_file, obj_mtime)) {
#ifdef CBUILD_DEBUG_SIGNATURE
            fprintf(stderr, "DEBUG: Header dep changed or missing for %s\n", src_file);
#endif
            return 1;
        }
    }

    return 0;
}

static int compile_source(cbuild_context_t* ctx, const char* src_file, const char* obj_file,
                          const char* dep_file, target_t* t) {
    ensure_dir_exists(t->obj_dir);

    cbuild_argv_t argv;
    cbuild_argv_init(&argv);
    cbuild__compile_argv(ctx, t, src_file, obj_file, dep_file, &argv);

    // Print command in verbose mode before executing
    if (ctx->verbose) {
        char cmd_buf[4096];
        int pos = 0;
        for (int i = 0; i < argv.count && pos < (int)sizeof(cmd_buf) - 1; ++i) {
            if (i > 0) cmd_buf[pos++] = ' ';
            if (strchr(argv.args[i], ' ')) {
                pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "'%s'", argv.args[i]);
            } else {
                pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "%s", argv.args[i]);
            }
        }
        cmd_buf[pos] = '\0';
        cbuild__log(ctx, CBUILD_LOG_VERBOSE, "%s", cmd_buf);
    }

    int result;
    char* output = NULL;
    result = cbuild_spawn_process(ctx, &argv, 1, &output);

    /* Report failures first: the dependency scan below tokenizes output in place */
    if (output && result != 0) {
        fwrite(output, 1, strlen(output), stderr);
    }

    /* MSVC reports headers on stdout (/showIncludes). Only a successful compile
     * may replace the dependency list: a failed one stops before listing them all. */
    if (output && result == 0 && dep_file && cbuild_target_cc_kind(ctx, t) == CBUILD_CC_MSVC) {
        FILE* df = fopen(dep_file, "w");
        if (df) {
            fprintf(df, "CBUILD_MSVC_DEPS_V1\n%s\n", src_file);
            char* saveptr = NULL;
            char* line = strtok_r(output, "\r\n", &saveptr);
            /* Allow override of the include tag for non-English locales */
            const char* include_tag = getenv("CBUILD_MSVC_INCLUDE_TAG");
            if (!include_tag) include_tag = "Note: including file:";
            while (line) {
                char* pos = strstr(line, include_tag);
                if (pos) {
                    pos += strlen(include_tag);
                    while (*pos == ' ' || *pos == '\t')
                        pos++;
                    if (*pos) {
                        fprintf(df, "%s\n", pos);
                    }
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
            fclose(df);
        }
    }
    if (output)
        free(output);

    /* Write compile signature for change detection (only on success) */
    char sigpath[1024];
    snprintf(sigpath, sizeof(sigpath), "%s.sig", obj_file);
    if (result == 0) {
        char* sig = cbuild__compile_signature(&argv);
        FILE* sf = fopen(sigpath, "wb");
        if (sf) {
            if (sig) fwrite(sig, 1, strlen(sig), sf);
            fclose(sf);
        }
        if (sig) free(sig);
    } else {
        /* Not every compiler deletes the previous object when it fails. Left in
         * place it could pass for up to date once the error is "fixed" by
         * restoring an older file. */
        remove(obj_file);
        remove(sigpath);
    }

    cbuild_argv_free(&argv);

    if (result != 0) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Compilation failed for %s", src_file);
    }
    return result;
}

static void enqueue_compile_job(cbuild_context_t* ctx, target_t* target, int source_index) {
    if (ctx->job_count >= ctx->job_capacity) {
        ctx->job_capacity = ctx->job_capacity ? ctx->job_capacity * 2 : 32;
        ctx->job_queue = realloc(ctx->job_queue, ctx->job_capacity * sizeof(compile_job_t));
    }

    ctx->job_queue[ctx->job_count].target = target;
    ctx->job_queue[ctx->job_count].source_index = source_index;
    ctx->job_count++;
}

#ifdef _WIN32

static DWORD WINAPI compile_worker(void* arg) {
    compile_worker_arg_t* warg = (compile_worker_arg_t*)arg;
    cbuild_context_t* ctx = warg->ctx;
    while (1) {
        EnterCriticalSection(&ctx->queue_mutex);
        if (ctx->jobs_completed >= ctx->job_count || ctx->build_error) {
            LeaveCriticalSection(&ctx->queue_mutex);
            break;
        }

        int job_index = ctx->jobs_completed++;
        compile_job_t job = ctx->job_queue[job_index];
        LeaveCriticalSection(&ctx->queue_mutex);

        target_t* t = job.target;
        int i = job.source_index;
        const char* src = t->sources[i];
        const char* slash = strrchr(src, '/');
        const char* bslash = strrchr(src, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        const char* base = slash ? slash + 1 : src;
        char* dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        unsigned h = cbuild__hash_path(src);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s-%08x" CBUILD_OBJ_EXT, t->obj_dir, (int)len, base, h);

        char depname[512];
        snprintf(depname, sizeof(depname), "%s/%.*s-%08x" CBUILD_OBJ_EXT ".d", t->obj_dir, (int)len, base, h);

        if (need_recompile(ctx, src, objname, depname, t)) {
            cbuild__log_step(ctx, "COMPILE", CBUILD_COLOR_BLUE, "%s", src);
            if (compile_source(ctx, src, objname, depname, t) != 0) {
                EnterCriticalSection(&ctx->queue_mutex);
                ctx->build_error = 1;
                LeaveCriticalSection(&ctx->queue_mutex);
                break;
            }
            EnterCriticalSection(&ctx->queue_mutex);
            ctx->compiled_count++;
            LeaveCriticalSection(&ctx->queue_mutex);
        }
    }
    return 0;
}

#else

static void* compile_worker(void* arg) {
    compile_worker_arg_t* warg = (compile_worker_arg_t*)arg;
    cbuild_context_t* ctx = warg->ctx;
    while (1) {
        pthread_mutex_lock(&ctx->queue_mutex);
        if (ctx->jobs_completed >= ctx->job_count || ctx->build_error) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            break;
        }

        int job_index = ctx->jobs_completed++;
        compile_job_t job = ctx->job_queue[job_index];
        pthread_mutex_unlock(&ctx->queue_mutex);

        target_t* t = job.target;
        int i = job.source_index;
        const char* src = t->sources[i];
        const char* slash = strrchr(src, '/');
        const char* bslash = strrchr(src, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        const char* base = slash ? slash + 1 : src;
        char* dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        unsigned h = cbuild__hash_path(src);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s-%08x" CBUILD_OBJ_EXT, t->obj_dir, (int)len, base, h);

        char depname[512];
        snprintf(depname, sizeof(depname), "%s/%.*s-%08x" CBUILD_OBJ_EXT ".d", t->obj_dir, (int)len, base, h);

        if (need_recompile(ctx, src, objname, depname, t)) {
            cbuild__log_step(ctx, "COMPILE", CBUILD_COLOR_BLUE, "%s", src);
            if (compile_source(ctx, src, objname, depname, t) != 0) {
                pthread_mutex_lock(&ctx->queue_mutex);
                ctx->build_error = 1;
                pthread_mutex_unlock(&ctx->queue_mutex);
                break;
            }
            pthread_mutex_lock(&ctx->queue_mutex);
            ctx->compiled_count++;
            pthread_mutex_unlock(&ctx->queue_mutex);
        }
    }
    return NULL;
}

#endif

CBUILD_INTERNAL int process_compile_jobs_parallel(cbuild_context_t* ctx, target_t* target, int* error_flag) {
    ctx->job_count = 0;
    ctx->jobs_completed = 0;
    ctx->compiled_count = 0;
    ctx->build_error = 0;

    for (int i = 0; i < target->sources_count; ++i) {
        enqueue_compile_job(ctx, target, i);
    }

    if (ctx->job_count == 0) return 0;

    compile_worker_arg_t warg;
    warg.ctx = ctx;

    int thread_count = ctx->parallel_jobs < ctx->job_count ? ctx->parallel_jobs : ctx->job_count;

#ifdef _WIN32
    InitializeCriticalSection(&ctx->queue_mutex);
#else
    pthread_mutex_init(&ctx->queue_mutex, NULL);
#endif

    ctx->threads = malloc(thread_count * sizeof(*ctx->threads));

#ifdef _WIN32
    for (int i = 0; i < thread_count; ++i) {
        ctx->threads[i] = CreateThread(NULL, 0, compile_worker, &warg, 0, NULL);
    }
#else
    for (int i = 0; i < thread_count; ++i) {
        pthread_create(&ctx->threads[i], NULL, compile_worker, &warg);
    }
#endif

#ifdef _WIN32
    WaitForMultipleObjects(thread_count, ctx->threads, TRUE, INFINITE);
    for (int i = 0; i < thread_count; ++i) {
        CloseHandle(ctx->threads[i]);
    }
    DeleteCriticalSection(&ctx->queue_mutex);
#else
    for (int i = 0; i < thread_count; ++i) {
        pthread_join(ctx->threads[i], NULL);
    }
    pthread_mutex_destroy(&ctx->queue_mutex);
#endif

    free(ctx->threads);
    ctx->threads = NULL;

    if (ctx->job_queue) {
        free(ctx->job_queue);
        ctx->job_queue = NULL;
        ctx->job_capacity = 0;
    }

    if (ctx->build_error) {
        *error_flag = 1;
    }
    return ctx->compiled_count;
}

CBUILD_INTERNAL void collect_compile_commands_for_target(cbuild_context_t* ctx, target_t* t) {
    if (!ctx->generate_compile_commands)
        return;
    /* File-dep "sources" are generator inputs, not translation units. */
    if (t->external || !t->obj_dir ||
        (t->type != TARGET_EXECUTABLE && t->type != TARGET_STATIC_LIB && t->type != TARGET_SHARED_LIB))
        return;
    cbuild__apply_config_if_needed(ctx, t);
    for (int i = 0; i < t->sources_count; ++i) {
        const char* src_file = t->sources[i];
        const char* slash = strrchr(src_file, '/');
        const char* bslash = strrchr(src_file, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        const char* base = slash ? slash + 1 : src_file;
        char* dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        unsigned h = cbuild__hash_path(src_file);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s-%08x" CBUILD_OBJ_EXT, t->obj_dir, (int)len, base, h);

        /* Same argv as compile_source, minus -MMD/-MF: those are for build
           system use, not semantic analysis */
        cbuild_argv_t argv;
        cbuild_argv_init(&argv);
        cbuild__compile_argv(ctx, t, src_file, objname, NULL, &argv);

        /* Convert argv to command string for compatibility */
        char* cmd = cbuild_argv_to_cmdline(&argv);

        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            if (ctx->cc_count + 1 > ctx->cc_cap) {
                ctx->cc_cap = ctx->cc_cap ? ctx->cc_cap * 2 : 4;
                ctx->cc_entries = realloc(ctx->cc_entries, ctx->cc_cap * sizeof(*ctx->cc_entries));
            }
            ctx->cc_entries[ctx->cc_count].directory = strdup(cwd);
            ctx->cc_entries[ctx->cc_count].command = cmd;

            /* Store arguments array for tools that prefer it */
            ctx->cc_entries[ctx->cc_count].argc = argv.count;
            ctx->cc_entries[ctx->cc_count].arguments = (char**)malloc(argv.count * sizeof(char*));
            for (int j = 0; j < argv.count; ++j) {
                ctx->cc_entries[ctx->cc_count].arguments[j] = strdup(argv.args[j]);
            }

            ctx->cc_entries[ctx->cc_count].file = strdup(src_file);
            ctx->cc_count++;
        } else {
            free(cmd);
        }

        cbuild_argv_free(&argv);
    }
}
