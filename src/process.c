/* process.c - argv building and process spawning. */

#include "cbuild_internal.h"

CBUILD_INTERNAL void cbuild_argv_init(cbuild_argv_t* argv) {
    argv->args = NULL;
    argv->count = 0;
    argv->capacity = 0;
}

CBUILD_INTERNAL void cbuild_argv_free(cbuild_argv_t* argv) {
    if (argv->args) {
        for (int i = 0; i < argv->count; ++i) {
            free(argv->args[i]);
        }
        free(argv->args);
    }
    argv->args = NULL;
    argv->count = 0;
    argv->capacity = 0;
}

CBUILD_INTERNAL int cbuild_argv_append(cbuild_argv_t* argv, const char* arg) {
    if (argv->count >= argv->capacity) {
        int new_cap = argv->capacity == 0 ? 16 : argv->capacity * 2;
        char** new_args = (char**)realloc(argv->args, (new_cap + 1) * sizeof(char*));
        if (!new_args) {
            return -1;
        }
        argv->args = new_args;
        argv->capacity = new_cap;
    }
    argv->args[argv->count] = strdup(arg);
    if (!argv->args[argv->count]) {
        return -1;
    }
    argv->count++;
    argv->args[argv->count] = NULL; /* NULL-terminate for execvp */
    return 0;
}

#ifdef _WIN32

/* Windows: Build command line from argv for CreateProcess */
CBUILD_INTERNAL char* cbuild_argv_to_cmdline(cbuild_argv_t* argv) {
    size_t total_len = 0;
    for (int i = 0; i < argv->count; ++i) {
        const char* arg = argv->args[i];
        int needs_quote = 0;
        if (strchr(arg, ' ') || strchr(arg, '\t') || strchr(arg, '"') || *arg == '\0') {
            needs_quote = 1;
        }
        if (needs_quote) total_len += 2; /* quotes */
        for (const char* p = arg; *p; ++p) {
            if (*p == '"')
                total_len += 2; /* \" */
            else if (*p == '\\') {
                const char* q = p;
                int num_backslash = 0;
                while (*q == '\\') {
                    q++;
                    num_backslash++;
                }
                if (*q == '"' || *q == '\0') {
                    total_len += num_backslash * 2;
                    p = q - 1;
                } else {
                    total_len += num_backslash;
                    p = q - 1;
                }
            } else {
                total_len += 1;
            }
        }
        if (i > 0) total_len += 1; /* space separator */
    }

    char* cmdline = (char*)malloc(total_len + 1);
    if (!cmdline) return NULL;
    char* out = cmdline;

    for (int i = 0; i < argv->count; ++i) {
        if (i > 0) *out++ = ' ';
        const char* arg = argv->args[i];
        int needs_quote = 0;
        if (strchr(arg, ' ') || strchr(arg, '\t') || strchr(arg, '"') || *arg == '\0') {
            needs_quote = 1;
        }
        if (needs_quote) *out++ = '"';

        for (const char* p = arg; *p; ++p) {
            if (*p == '"') {
                *out++ = '\\';
                *out++ = '"';
            } else if (*p == '\\') {
                const char* q = p;
                int num_backslash = 0;
                while (*q == '\\') {
                    q++;
                    num_backslash++;
                }
                if (*q == '"' || *q == '\0') {
                    for (int k = 0; k < num_backslash * 2; ++k) *out++ = '\\';
                    p = q - 1;
                } else {
                    for (int k = 0; k < num_backslash; ++k) *out++ = '\\';
                    p = q - 1;
                }
            } else {
                *out++ = *p;
            }
        }
        if (needs_quote) *out++ = '"';
    }
    *out = '\0';
    return cmdline;
}

#else

/* Unix/Linux: Build command line from argv for compile_commands.json */
CBUILD_INTERNAL char* cbuild_argv_to_cmdline(cbuild_argv_t* argv) {
    size_t total_len = 0;
    for (int i = 0; i < argv->count; ++i) {
        const char* arg = argv->args[i];
        int needs_quote = 0;
        /* Check if argument needs quoting */
        for (const char* p = arg; *p; ++p) {
            if (*p == ' ' || *p == '\t' || *p == '\'' || *p == '"' || *p == '\\' ||
                *p == '$' || *p == '`' || *p == '!' || *p == '*' || *p == '?' ||
                *p == '[' || *p == ']' || *p == '(' || *p == ')' || *p == '{' || *p == '}' ||
                *p == '&' || *p == '|' || *p == ';' || *p == '<' || *p == '>') {
                needs_quote = 1;
                break;
            }
        }
        if (*arg == '\0') needs_quote = 1;

        if (needs_quote) {
            total_len += 2; /* quotes */
            for (const char* p = arg; *p; ++p) {
                if (*p == '\'' || *p == '\\' || *p == '"') {
                    total_len += 2; /* escape char + char */
                } else {
                    total_len += 1;
                }
            }
        } else {
            total_len += strlen(arg);
        }
        if (i > 0) total_len += 1; /* space separator */
    }

    char* cmdline = (char*)malloc(total_len + 1);
    if (!cmdline) return NULL;
    char* out = cmdline;

    for (int i = 0; i < argv->count; ++i) {
        if (i > 0) *out++ = ' ';
        const char* arg = argv->args[i];
        int needs_quote = 0;

        /* Check if argument needs quoting */
        for (const char* p = arg; *p; ++p) {
            if (*p == ' ' || *p == '\t' || *p == '\'' || *p == '"' || *p == '\\' ||
                *p == '$' || *p == '`' || *p == '!' || *p == '*' || *p == '?' ||
                *p == '[' || *p == ']' || *p == '(' || *p == ')' || *p == '{' || *p == '}' ||
                *p == '&' || *p == '|' || *p == ';' || *p == '<' || *p == '>') {
                needs_quote = 1;
                break;
            }
        }
        if (*arg == '\0') needs_quote = 1;

        if (needs_quote) {
            *out++ = '"';
            for (const char* p = arg; *p; ++p) {
                if (*p == '"' || *p == '\\' || *p == '$' || *p == '`') {
                    *out++ = '\\';
                }
                *out++ = *p;
            }
            *out++ = '"';
        } else {
            for (const char* p = arg; *p; ++p) {
                *out++ = *p;
            }
        }
    }
    *out = '\0';
    return cmdline;
}

#endif

CBUILD_INTERNAL int cbuild_spawn_process(cbuild_context_t* ctx, cbuild_argv_t* argv, int capture_output, char** captured_output) {
    if (ctx && ctx->verbose && !capture_output) {
        char cmd_buf[4096];
        int pos = 0;
        for (int i = 0; i < argv->count && pos < (int)sizeof(cmd_buf) - 1; ++i) {
            if (i > 0) cmd_buf[pos++] = ' ';
            if (strchr(argv->args[i], ' ')) {
                pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "'%s'", argv->args[i]);
            } else {
                pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "%s", argv->args[i]);
            }
        }
        cmd_buf[pos] = '\0';
        cbuild__log(ctx, CBUILD_LOG_VERBOSE, "%s", cmd_buf);
    }

#ifdef _WIN32
    HANDLE hOutputRead = NULL, hOutputWrite = NULL;
    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (capture_output) {
        if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0)) {
            return -1;
        }
        SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(STARTUPINFOA);
    if (capture_output) {
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = hOutputWrite;
        si.hStdError = hOutputWrite;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    PROCESS_INFORMATION pi = { 0 };
    char* cmdline = cbuild_argv_to_cmdline(argv);
    if (!cmdline) {
        if (capture_output) {
            CloseHandle(hOutputRead);
            CloseHandle(hOutputWrite);
        }
        return -1;
    }

    BOOL success = CreateProcessA(
        NULL,
        cmdline,
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi);

    free(cmdline);

    if (!success) {
        if (capture_output) {
            CloseHandle(hOutputRead);
            CloseHandle(hOutputWrite);
        }
        return -1;
    }

    if (capture_output) {
        CloseHandle(hOutputWrite);

        *captured_output = NULL;
        size_t out_len = 0;
        char buffer[4096];
        DWORD bytes_read;

        while (ReadFile(hOutputRead, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
            char* new_output = (char*)realloc(*captured_output, out_len + bytes_read + 1);
            if (!new_output) {
                free(*captured_output);
                *captured_output = NULL;
                CloseHandle(hOutputRead);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return -1;
            }
            *captured_output = new_output;
            memcpy(*captured_output + out_len, buffer, bytes_read);
            out_len += bytes_read;
            (*captured_output)[out_len] = '\0';
        }

        CloseHandle(hOutputRead);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exit_code;

#else
    /* POSIX: try posix_spawn first, fall back to fork/execvp */
    int pipefd[2] = { -1, -1 };

    if (capture_output) {
        if (pipe(pipefd) != 0) {
            return -1;
        }
    }

    pid_t pid;

#ifdef __APPLE__
    /* macOS: use posix_spawn (preferred) */
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (capture_output) {
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    }

    int spawn_result = posix_spawnp(&pid, argv->args[0], &actions, NULL, argv->args, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_result != 0) {
        if (capture_output) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
        return -1;
    }

#else
    /* Linux and other POSIX: use fork/execvp */
    pid = fork();
    if (pid < 0) {
        if (capture_output) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        if (capture_output) {
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
        }
        execvp(argv->args[0], argv->args);
        _exit(127); /* execvp failed */
    }
#endif

    /* Parent process */
    if (capture_output) {
        close(pipefd[1]);

        *captured_output = NULL;
        size_t out_len = 0;
        char buffer[4096];
        ssize_t bytes_read;

        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            char* new_output = (char*)realloc(*captured_output, out_len + bytes_read + 1);
            if (!new_output) {
                free(*captured_output);
                *captured_output = NULL;
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return -1;
            }
            *captured_output = new_output;
            memcpy(*captured_output + out_len, buffer, bytes_read);
            out_len += bytes_read;
            (*captured_output)[out_len] = '\0';
        }

        close(pipefd[0]);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
#endif
}

/* Legacy shell-based command execution (deprecated, kept for compatibility) */
CBUILD_INTERNAL int run_command(cbuild_context_t* ctx, const char* cmd, int capture_out,
                                char** captured_output) {
    if (ctx && ctx->verbose && !capture_out) {
        cbuild__log(ctx, CBUILD_LOG_VERBOSE, "%s", cmd);
    }
    if (capture_out) {
#ifdef _WIN32
        FILE* pipe = _popen(cmd, "r");
#else
        FILE* pipe = popen(cmd, "r");
#endif
        if (!pipe) {
            return -1;
        }
        char buffer[256];
        size_t out_len = 0;
        *captured_output = NULL;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            size_t chunk = strlen(buffer);
            *captured_output = (char*)realloc(*captured_output, out_len + chunk + 1);
            if (!*captured_output) {
                cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Out of memory capturing command output");
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
                return -1;
            }
            memcpy(*captured_output + out_len, buffer, chunk);
            out_len += chunk;
            (*captured_output)[out_len] = '\0';
        }
#ifdef _WIN32
        int exitCode = _pclose(pipe);
#else
        int exitCode = pclose(pipe);
#endif
        if (exitCode == -1) {
            return -1;
        }
        return exitCode;
    } else {
#ifdef _WIN32
        /* Use PowerShell on Windows for better path and command handling */
        size_t cmd_len = strlen(cmd);
        char* ps_cmd = (char*)malloc(cmd_len + 128);
        if (!ps_cmd) return -1;
        snprintf(ps_cmd, cmd_len + 128, "powershell -NoProfile -ExecutionPolicy Bypass -Command \"%s\"", cmd);
        int ret = system(ps_cmd);
        free(ps_cmd);
        return ret;
#else
        int ret = system(cmd);
        return ret;
#endif
    }
}

CBUILD_INTERNAL void cbuild__argv_append_prefixed(cbuild_argv_t* argv, const char* prefix, const char* value) {
    char* arg = NULL;
    if (append_format(&arg, "%s%s", prefix, value) == 0) {
        cbuild_argv_append(argv, arg);
    }
    free(arg);
}

/* Helper to split space-separated flag strings into argv tokens */
/* Note: No caching - called from parallel worker threads, must be thread-safe */
CBUILD_INTERNAL void cbuild_argv_append_flags(cbuild_argv_t* argv, const char* flags) {
    if (!flags || !*flags) return;

    /* Tokenize the flags string */
    char* copy = strdup(flags);
    if (!copy) return;

    char* p = copy;
    while (*p) {
        /* Skip leading whitespace */
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        char* start = p;
        int in_quote = 0;

        /* Find end of token */
        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                p++;
            } else if (!in_quote && (*p == ' ' || *p == '\t')) {
                break;
            } else {
                p++;
            }
        }

        if (p > start) {
            char saved = *p;
            *p = '\0';

            /* Remove surrounding quotes if present */
            char* token = start;
            size_t len = strlen(token);
            if (len >= 2 && token[0] == '"' && token[len - 1] == '"') {
                token[len - 1] = '\0';
                token++;
            }

            cbuild_argv_append(argv, token);

            *p = saved;
        }
    }

    free(copy);
}
