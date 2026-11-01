/* self_rebuild.c - rebuild and re-exec the build program when its sources change. */

#include "cbuild_internal.h"

static int cbuild__needs_rebuild(const char* exe_path, const char** sources,
                                 int sources_count) {
    cbuild_mtime_t exe_mtime;
    if (cbuild__mtime(exe_path, &exe_mtime) != 0)
        return 1;
    for (int i = 0; i < sources_count; ++i) {
        cbuild_mtime_t src_mtime;
        if (cbuild__mtime(sources[i], &src_mtime) != 0)
            continue;
        if (src_mtime > exe_mtime)
            return 1;
    }
    return 0;
}

/* Replaces this run with the program at exe_path. Returns only when it could not be started. */
static void cbuild__exec_new_build(cbuild_context_t* ctx, const char* exe_path, int argc, char** argv) {
#ifdef _WIN32
    const char** args = (const char**)malloc(sizeof(char*) * (size_t)argc);
    if (!args) return;
    args[0] = exe_path;
    for (int i = 1; i < argc; ++i) args[i] = argv[i];
    fflush(NULL);
    int status = cbuild_spawn(ctx, args, argc, NULL);
    free(args);
    if (status != -1) exit(status);
#else
    (void)ctx;
    (void)argc;
    execv(exe_path, argv);
#endif
}

static const char* cbuild__self_rebuild_compiler(void) {
    const char* compiler = getenv("CC_FOR_BUILD");
    if (compiler && *compiler) return compiler;
    compiler = getenv("CC");
    if (compiler && *compiler) return compiler;
#ifdef _WIN32
    return "cl";
#else
    return "cc";
#endif
}

/* Returns a malloc'd command line for system, or NULL. */
static char* cbuild__self_rebuild_command(const char* compiler, const char* exe_path, const char* build_src) {
    char* cmd = NULL;
    if (detect_cc_kind(compiler) == CBUILD_CC_MSVC) {
        append_format(&cmd, "%s /nologo /Fe:\"%s\" \"%s\" /I. /Iinclude", compiler, exe_path, build_src);
    } else {
        append_format(&cmd, "%s -pthread -o \"%s\" \"%s\" -I. -Iinclude", compiler, exe_path, build_src);
    }
#ifdef _WIN32
    /* cmd.exe strips the first and last quote of a line that starts with a quote. */
    if (cmd && cmd[0] == '"') {
        char* wrapped = NULL;
        append_format(&wrapped, "\"%s\"", cmd);
        free(cmd);
        cmd = wrapped;
    }
#endif
    return cmd;
}

/* Writes the path of the running program to out, or argv0 when the system cannot report it. */
static void cbuild__self_path(char* out, size_t size, const char* argv0) {
#ifdef _WIN32
    (void)argv0;
    GetModuleFileNameA(NULL, out, (DWORD)size);
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(out, &sz) != 0) snprintf(out, size, "%s", argv0);
#elif defined(__FreeBSD__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t len = size;
    if (sysctl(mib, 4, out, &len, NULL, 0) != 0) snprintf(out, size, "%s", argv0);
#else
    ssize_t len = readlink("/proc/self/exe", out, size - 1);
    if (len > 0)
        out[len] = 0;
    else
        snprintf(out, size, "%s", argv0);
#endif
}

int cbuild_self_rebuild_if_needed(cbuild_context_t* ctx, int argc, char** argv, const char** sources,
                                  int sources_count) {
    char exe_path[512];
    cbuild__self_path(exe_path, sizeof(exe_path), argv[0]);

    // Always remove any lingering .old file
    char old_path[520];
    snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
    remove(old_path);

    // Pick the main build source: the first item passed to CBUILD_SELF_REBUILD(...)
    const char* build_src = (sources_count > 0 && sources[0] && *sources[0])
                                ? sources[0]
                                : "build.c";  // conservative fallback

    if (cbuild__needs_rebuild(exe_path, sources, sources_count)) {
        const char* compiler = cbuild__self_rebuild_compiler();
        cbuild__log(ctx, CBUILD_LOG_INFO, "cbuild: Detected changes, rebuilding build executable from %s with %s...",
                    build_src, compiler);
        int moved = rename(exe_path, old_path) == 0;

        char* cmd = cbuild__self_rebuild_command(compiler, exe_path, build_src);
        int rc = cmd ? system(cmd) : -1;
        free(cmd);
        if (rc == 0) cbuild__exec_new_build(ctx, exe_path, argc, argv);
        if (moved) {
            remove(exe_path);
            if (rename(old_path, exe_path) != 0) {
                cbuild__set_error(ctx, "Could not restore the build program from %s", old_path);
                cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: could not restore the build program from %s", old_path);
                return -1;
            }
        }
        if (rc != 0) {
            cbuild__set_error(ctx, "Self-rebuild with %s failed with exit code %d", compiler, rc);
            cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: could not rebuild with %s", compiler);
            return -1;
        }
        cbuild__log(ctx, CBUILD_LOG_WARNING,
                    "cbuild: could not start the rebuilt build program; continuing with the old build program "
                    "(set CC_FOR_BUILD to a compiler for this machine)");
    }
    return 0;
}
