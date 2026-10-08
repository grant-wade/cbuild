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

static int cbuild__exec_new_build(const char* exe_path, int argc,
                                  char** argv) {
    (void)argc;
#ifdef _WIN32
    _spawnv(_P_OVERLAY, exe_path, argv);
    return -1;
#else
    execv(exe_path, argv);
    return -1;
#endif
}

int cbuild_self_rebuild_if_needed(cbuild_context_t* ctx, int argc, char** argv, const char** sources,
                                  int sources_count) {
    char exe_path[512];
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
#elif defined(__APPLE__)
    uint32_t sz = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &sz) != 0) {
        strncpy(exe_path, argv[0], sizeof(exe_path));
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0)
        exe_path[len] = 0;
    else
        strncpy(exe_path, argv[0], sizeof(exe_path));
#endif

    // Always remove any lingering .old file
    char old_path[520];
    snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
    remove(old_path);

    // Pick the main build source: the first item passed to CBUILD_SELF_REBUILD(...)
    const char* build_src = (sources_count > 0 && sources[0] && *sources[0])
                                ? sources[0]
                                : "build.c";  // conservative fallback

    if (cbuild__needs_rebuild(exe_path, sources, sources_count)) {
        cbuild__log(ctx, CBUILD_LOG_INFO, "cbuild: Detected changes, rebuilding build executable from %s...", build_src);
        rename(exe_path, old_path);

        char cmd[1024];
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd),
                 "cl /nologo /Fe:\"%s\" \"%s\" /I. /Iinclude",
                 exe_path, build_src);
#else
        snprintf(cmd, sizeof(cmd),
                 "cc -o \"%s\" \"%s\" -I. -Iinclude",
                 exe_path, build_src);
#endif
        int rc = system(cmd);
        if (rc != 0) {
            cbuild__set_error(ctx, "Self-rebuild failed with exit code %d", rc);
            cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Self-rebuild failed!");
            return -1;
        }
        if (cbuild__exec_new_build(exe_path, argc, argv) != 0) {
            cbuild__set_error(ctx, "Failed to exec new build executable");
            return -1;
        }
    }
    return 0;
}
