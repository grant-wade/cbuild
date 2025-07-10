/*
------------------------------------------------------------------------------
cbuild.h - Minimal, cross-platform, header-only C build system

Copyright (c) 2025 Grant Wade

1. Purpose
----------
A minimal, cross-platform, header-only build system for C projects. 
Write your build logic directly in C, enabling full language power 
and portability without external tools or scripts.

2. Key Features
---------------
- Target management: executables, static/shared libraries, custom commands
- Dependency handling: tracks source/header changes, supports incremental builds
- Source/include/library management: add sources, includes, libraries (wildcards supported)
- Custom build logic: subcommands, pre/post build steps, user callbacks
- Incremental builds: only rebuilds what has changed
- Self-rebuilding: build executable can auto-rebuild when sources change
- Subproject support: build and link subprojects, fetch targets from other cbuild projects
- Compile_commands.json: optional generation for IDE tooling
- Macro helpers: batch add sources/includes/defines, target definition macros
- Platform abstraction: works on Windows, macOS, Linux

3. Supported Build Configurations
---------------------------------
- Debug/release and custom flags per target or globally
- Platform-specific output and compiler/linker flags
- Compiler selection (gcc, clang, cl, etc.)
- Feature flags and preprocessor defines (per-target and global)
- Parallelism control (auto-detects CPU count, overrideable)
- Dependency tracking (optional, .d file support)

4. Usage Example
----------------
    #define CBUILD_IMPLEMENTATION
    #include "cbuild.h"

    int main(int argc, char** argv) {
        cbuild_set_output_dir("build");
        target_t* lib = cbuild_static_library("foo");
        cbuild_add_source(lib, "foo.c");
        target_t* exe = cbuild_executable("bar");
        cbuild_add_source(exe, "bar.c");
        cbuild_target_link_library(exe, lib);
        return cbuild_run(argc, argv);
    }

Typical build commands:
    $ gcc build.c -o cbuild
    $ ./cbuild           # builds all targets
    $ ./cbuild clean     # cleans build outputs
    $ ./cbuild bar       # builds only 'bar' and its dependencies

5. Dependencies & Integration Notes
-----------------------------------
- No external dependencies: single header, ANSI C, no Python/Lua/tools required
- Integrate by including in your build.c and defining CBUILD_IMPLEMENTATION in one file
- Subproject support: build and link other cbuild-based projects
- Platform headers: handles Windows, macOS, Linux specifics internally
- Self-rebuild: call cbuild_self_rebuild_if_needed() or use CBUILD_SELF_REBUILD macro

6. Notable Macros & API
-----------------------
- CBUILD_IMPLEMENTATION: place in one .c file to enable implementation
- CBUILD_SELF_REBUILD(...): auto-rebuild build executable if sources change
- CBUILD_SOURCES, CBUILD_INCLUDES, CBUILD_LIB_DIRS, CBUILD_LINK_LIBS: batch add helpers
- CBUILD_EXECUTABLE, CBUILD_STATIC_LIBRARY, CBUILD_SHARED_LIBRARY: target definition helpers
- CBUILD_DEFINES: batch add preprocessor macros
- CBUILD_SUBPROJECT: declare and initialize subprojects

7. Documentation
----------------
- Doxygen-style comments and usage instructions are provided throughout this header.
- See function and macro documentation below for details.

8. License
----------
BSD 3-Clause License

Copyright (c) 2025 Grant Wade
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
------------------------------------------------------------------------------
*/

#ifndef CBUILD_H
#define CBUILD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef target_t
 * @brief Opaque handle representing a build target (executable or library).
 */
typedef struct cbuild_target target_t;

/**
 * @typedef command_t
 * @brief Opaque handle representing a build command to be executed.
 */
typedef struct cbuild_command command_t;

/**
 * @typedef subproject_t
 * @brief Opaque handle representing a subproject within the build system.
 */
typedef struct cbuild_subproject subproject_t;

/**
 * @brief Callback type for custom subcommands.
 * @param user_data Pointer to user-defined data passed to the callback.
 */
typedef void (*cbuild_subcommand_callback)(void *user_data);

/**
 * @brief Create a new executable build target.
 *
 * @param name Name of the executable target (no file extension needed).
 * @return Pointer to the created target object.
 */
target_t *cbuild_executable(const char *name);

/**
 * @brief Create a new static library build target.
 *
 * On Unix, the 'lib' prefix and .a extension will be added automatically.
 *
 * @param name Name of the static library target.
 * @return Pointer to the created target object.
 */
target_t *cbuild_static_library(const char *name);

/**
 * @brief Create a new shared library build target.
 *
 * Adds the appropriate extension (.dll, .so, or .dylib) based on the platform.
 *
 * @param name Name of the shared library target.
 * @return Pointer to the created target object.
 */
target_t *cbuild_shared_library(const char *name);

/**
 * @brief Create a shell command to be run as part of the build graph.
 *
 * The command string is executed as-is by the shell.
 *
 * @param name Name of the command.
 * @param command_line Shell command to execute.
 * @return Pointer to the created command object.
 */
command_t *cbuild_command(const char *name, const char *command_line);

/**
 * @brief Add a command as a dependency of a target.
 *
 * The command will run before the target is built.
 *
 * @param target Target to add the command dependency to.
 * @param cmd Command to add as a dependency.
 */
void cbuild_target_add_command(target_t *target, command_t *cmd);

/**
 * @brief Add a command as a dependency of another command.
 *
 * The dependency command will run before the main command.
 *
 * @param cmd Command to add the dependency to.
 * @param dependency Command to add as a dependency.
 */
void cbuild_command_add_dependency(command_t *cmd, command_t *dependency);

/**
 * @brief Run a command immediately (not as part of the build graph).
 *
 * @param cmd Command to run.
 * @return 0 on success, nonzero on failure.
 */
int cbuild_run_command(command_t *cmd);

/**
 * @brief Add a command to be run after the target is built (post-build step).
 *
 * @param target Target to add the post-build command to.
 * @param cmd Command to run after the target is built.
 */
void cbuild_target_add_post_command(target_t *target, command_t *cmd);

/**
 * @brief Add a source file to a target.
 *
 * The source file can be C or C++ source code. Wildcards are supported.
 *
 * @param target Target to add the source file to.
 * @param source_file Path to the source file.
 */
void cbuild_add_source(target_t *target, const char *source_file);

/**
 * @brief Add an include directory for a target.
 *
 * The directory is passed to the compiler as -I or /I.
 *
 * @param target Target to add the include directory to.
 * @param include_path Path to the include directory.
 */
void cbuild_add_include_dir(target_t *target, const char *include_path);

/**
 * @brief Add a library search directory for a target's link phase.
 *
 * The directory is passed to the linker as -L or /LIBPATH.
 *
 * @param target Target to add the library directory to.
 * @param lib_dir Path to the library directory.
 */
void cbuild_add_library_dir(target_t *target, const char *lib_dir);

/**
 * @brief Link an external library to a target by name.
 *
 * For GCC/Clang, use names like "m" for math (adds -lm).
 * For MSVC, use the library base name (e.g., "User32" for User32.lib).
 *
 * @param target Target to link the library to.
 * @param lib_name Name of the library to link.
 */
void cbuild_add_link_library(target_t *target, const char *lib_name);

/**
 * @brief Declare that one target links against another target.
 *
 * The dependency target will be built first, and its output will be linked into the dependant.
 *
 * @param dependant Target that depends on the other.
 * @param dependency Target to be linked as a dependency.
 */
void cbuild_target_link_library(target_t *dependant, target_t *dependency);

/**
 * @brief Override global CFLAGS for a specific target.
 *
 * @param target Target to set custom CFLAGS for.
 * @param cflags Compiler flags to use for this target.
 */
void cbuild_target_add_cflags(target_t *target, const char *cflags);

/**
 * @brief Set a custom output directory for all build artifacts.
 *
 * This affects object files, libraries, and executables. Default is "build".
 *
 * @param dir Path to the output directory.
 */
void cbuild_set_output_dir(const char *dir);

/**
 * @brief Set the number of parallel compile jobs.
 *
 * Default is the number of CPU cores (at least 1).
 *
 * @param jobs_count Number of parallel jobs.
 */
void cbuild_set_parallelism(int jobs_count);

/**
 * @brief Manually specify the C compiler to use.
 *
 * If not set, cbuild auto-detects or uses the environment variable CC.
 *
 * @param compiler_exe Name or path of the compiler executable (e.g., "gcc", "clang", "cl").
 */
void cbuild_set_compiler(const char *compiler_exe);

/**
 * @brief Specify additional global compiler flags.
 *
 * These flags are applied to all targets. Optional.
 *
 * @param flags Compiler flags to add globally.
 */
void cbuild_add_global_cflags(const char *flags);

/**
 * @brief Specify additional global linker flags for executables/shared libraries.
 *
 * Optional.
 *
 * @param flags Linker flags to add globally.
 */
void cbuild_add_global_ldflags(const char *flags);

/**
 * @brief Enable or disable dependency tracking.
 *
 * Enables header dependency detection and .d file generation. Disabled by default.
 *
 * @param enabled Nonzero to enable, zero to disable.
 */
void cbuild_enable_dep_tracking(int enabled);

/**
 * @brief Automatically rebuild the build executable if any source files have changed.
 *
 * Checks if the running executable is out-of-date with respect to the given sources.
 * If so, moves itself to .old, rebuilds, and execs the new binary with the same arguments.
 * Call this at the start of main().
 *
 * @param argc main's argc
 * @param argv main's argv
 * @param sources Array of source file paths (e.g. {"build.c", "cbuild.h"})
 * @param sources_count Number of source files
 */
void cbuild_self_rebuild_if_needed(int argc, char **argv, const char **sources,
                                   int sources_count);

/**
 * @brief Enable or disable generation of compile_commands.json.
 *
 * @param enabled Nonzero to enable, zero to disable.
 */
void cbuild_enable_compile_commands(int enabled);

/**
 * @brief Declare a subproject to be included in the build.
 *
 * @param alias      An arbitrary name for this project (used to scope its proxy targets).
 * @param directory  Path to the subproject's root (where build.c lives).
 * @param cbuild_exe Path to the cbuild driver to invoke (usually "../cbuild").
 * @return           A handle to the subproject.
 */
subproject_t *cbuild_add_subproject(const char *alias, const char *directory,
                                    const char *cbuild_exe);

/**
 * @brief Fetch one of the subproject’s built targets by name.
 *
 * This creates a proxy target_t* in the current graph which:
 *  - depends on the subproject build command
 *  - has its output_file set to the subproject’s artifact path
 *  - can be passed to cbuild_target_link_library(), cbuild_add_library_dir(), etc.
 *
 * @param sub      Subproject handle.
 * @param tgt_name Name of the target to fetch from the subproject.
 * @return         Proxy target handle, or NULL on error (no such target in the manifest).
 */
target_t *cbuild_subproject_get_target(subproject_t *sub, const char *tgt_name);

/**
 * @def CBUILD_SUBPROJECT(ALIAS, DIR, EXE)
 * @brief Convenience macro for declaring a subproject.
 *
 * Declares and initializes a subproject handle named ALIAS.
 *
 * @param ALIAS Variable name for the subproject handle.
 * @param DIR   Path to the subproject directory.
 * @param EXE   Path to the cbuild executable for the subproject.
 */
#define CBUILD_SUBPROJECT(ALIAS, DIR, EXE) \
    subproject_t *ALIAS = cbuild_add_subproject(#ALIAS, DIR, EXE)

/**
 * @brief Execute the build process.
 *
 * Call this in main() with the program arguments.
 * Recognized commands:
 *   - no arguments: build all targets
 *   - "clean": remove built files
 *   - target name(s): build only those targets (and their dependencies)
 *
 * @param argc Argument count from main().
 * @param argv Argument vector from main().
 * @return 0 on success, nonzero on failure.
 */
int cbuild_run(int argc, char **argv);

/**
 * @brief Register a custom subcommand for the build system.
 *
 * @param name         The subcommand name (e.g. "test").
 * @param target       The target that must be built before the subcommand runs.
 * @param command_line Shell command to run (optional, can be NULL if using callback).
 * @param callback     User callback function to run (optional, can be NULL if using command_line).
 * @param user_data    User data pointer passed to callback (can be NULL).
 */
void cbuild_register_subcommand(const char *name, target_t *target,
                                const char *command_line,
                                cbuild_subcommand_callback callback,
                                void *user_data);

/**
 * @brief Define a preprocessor macro for a specific target.
 *
 * Equivalent to passing -DMACRO (or /DMACRO on MSVC).
 *
 * @param target Target to add the macro to.
 * @param macro  Macro name to define.
 */
void cbuild_add_define(target_t *target, const char *macro);

/**
 * @brief Define a preprocessor macro with a value for a specific target.
 *
 * Equivalent to passing -DMACRO=VALUE (or /DMACRO=VALUE on MSVC).
 *
 * @param target Target to add the macro to.
 * @param macro  Macro name to define.
 * @param value  Value to assign to the macro.
 */
void cbuild_add_define_val(target_t *target,
                           const char *macro,
                           const char *value);

/**
 * @brief Toggle a boolean feature flag for a target.
 *
 * true  → -DFLAG=1 , false → -DFLAG=0
 *
 * @param target Target to set the flag for.
 * @param flag   Name of the flag.
 * @param value  Boolean value (nonzero for true, zero for false).
 */
void cbuild_set_flag(target_t *target, const char *flag, int value);

/**
 * @brief Define a global preprocessor macro for all targets.
 *
 * @param macro Macro name to define globally.
 */
void cbuild_add_global_define(const char *macro);

/**
 * @brief Define a global preprocessor macro with a value for all targets.
 *
 * @param macro Macro name to define globally.
 * @param value Value to assign to the macro.
 */
void cbuild_add_global_define_val(const char *macro, const char *value);

/**
 * @brief Toggle a global boolean feature flag for all targets.
 *
 * @param flag  Name of the flag.
 * @param value Boolean value (nonzero for true, zero for false).
 */
void cbuild_set_global_flag(const char *flag, int value);

/**
 * @brief Check if a file exists.
 *
 * @param path Path to the file.
 * @return 1 if the file exists, 0 otherwise.
 */
int cbuild_file_exists(const char *path);

// Helper: check if a directory exists
/**
 * @brief Check if a directory exists.
 *
 * @param path Path to the directory.
 * @return 1 if the directory exists, 0 otherwise.
 */
int cbuild_dir_exists(const char *path);

// Helper: remove a file
/**
 * @brief Remove a file from the filesystem.
 *
 * @param path Path to the file.
 * @return 0 on success, -1 on failure.
 */
int cbuild_remove_file(const char *path);

// Helper: remove a directory recursively
/**
 * @brief Remove a directory and its contents recursively.
 *
 * @param path Path to the directory.
 * @return 0 on success, -1 on failure.
 */
int cbuild_remove_dir(const char *path);

// Helper: get the current working directory
/**
 * @brief Get the current working directory.
 *
 * @param buf  Buffer to store the current working directory path.
 * @param size Size of the buffer.
 * @return 0 on success, -1 on failure.
 */
int cbuild_get_cwd(char *buf, long size);


/**
 * @def CBUILD_SELF_REBUILD(...)
 * @brief Macro to self-rebuild the build executable if any listed source files change.
 *
 * Expands to a call to cbuild_self_rebuild_if_needed() with the provided source files.
 *
 * @param ... List of source file paths (e.g., "build.c", "cbuild.h").
 */
#define CBUILD_SELF_REBUILD(...)                                              \
    do {                                                                      \
        const char *_srcs[] = { __VA_ARGS__ };                                \
        cbuild_self_rebuild_if_needed(argc, argv, _srcs,                      \
                                      (int)(sizeof(_srcs) / sizeof(*_srcs))); \
    } while (0)

/**
 * @def CBUILD_SOURCES(TGT, ...)
 * @brief Macro to add multiple source files to a target.
 *
 * @param TGT  Target to add sources to.
 * @param ...  List of source file paths.
 */
#define CBUILD_SOURCES(TGT, ...)                                     \
    do {                                                             \
        const char *_a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_source(TGT, _a[_i]);                          \
    } while (0)

/**
 * @def CBUILD_INCLUDES(TGT, ...)
 * @brief Macro to add multiple include directories to a target.
 *
 * @param TGT  Target to add include directories to.
 * @param ...  List of include directory paths.
 */
#define CBUILD_INCLUDES(TGT, ...)                                    \
    do {                                                             \
        const char *_a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_include_dir(TGT, _a[_i]);                     \
    } while (0)

/**
 * @def CBUILD_LIB_DIRS(TGT, ...)
 * @brief Macro to add multiple library directories to a target.
 *
 * @param TGT  Target to add library directories to.
 * @param ...  List of library directory paths.
 */
#define CBUILD_LIB_DIRS(TGT, ...)                                    \
    do {                                                             \
        const char *_a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_library_dir(TGT, _a[_i]);                     \
    } while (0)

/**
 * @def CBUILD_LINK_LIBS(TGT, ...)
 * @brief Macro to link multiple libraries to a target.
 *
 * @param TGT  Target to link libraries to.
 * @param ...  List of library names or paths.
 */
#define CBUILD_LINK_LIBS(TGT, ...)                                   \
    do {                                                             \
        const char *_a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_link_library(TGT, _a[_i]);                    \
    } while (0)

/**
 * @def CBUILD_EXECUTABLE(NAME, ...)
 * @brief Macro to define an executable target with sources, includes, lib dirs, and libs.
 *
 * @param NAME Variable name for the target.
 * @param ...  Additional build configuration statements.
 */
#define CBUILD_EXECUTABLE(NAME, ...)     \
    do {                                 \
        NAME = cbuild_executable(#NAME); \
        __VA_ARGS__                      \
    } while (0)

/**
 * @def CBUILD_STATIC_LIBRARY(NAME, ...)
 * @brief Macro to define a static library target with sources.
 *
 * @param NAME Variable name for the target.
 * @param ...  Additional build configuration statements.
 */
#define CBUILD_STATIC_LIBRARY(NAME, ...)     \
    do {                                     \
        NAME = cbuild_static_library(#NAME); \
        __VA_ARGS__                          \
    } while (0)

/**
 * @def CBUILD_SHARED_LIBRARY(NAME, ...)
 * @brief Macro to define a shared library target with sources.
 *
 * @param NAME Variable name for the target.
 * @param ...  Additional build configuration statements.
 */
#define CBUILD_SHARED_LIBRARY(NAME, ...)     \
    do {                                     \
        NAME = cbuild_shared_library(#NAME); \
        __VA_ARGS__                          \
    } while (0)

/**
 * @def CBUILD_DEFINES(TGT, ...)
 * @brief Macro to define multiple preprocessor macros for a target.
 *
 * @param TGT  Target to add macros to.
 * @param ...  List of macro names.
 */
#define CBUILD_DEFINES(TGT, ...)                                           \
    do {                                                                   \
        const char *_defs[] = { __VA_ARGS__ };                             \
        for (int _i = 0; _i < (int)(sizeof(_defs) / sizeof(*_defs)); _i++) \
            cbuild_add_define((TGT), _defs[_i]);                           \
    } while (0)


#ifdef __cplusplus
}
#endif

/* ---------------------------------------------------------------------- */
/* Implementation below (define CBUILD_IMPLEMENTATION in one source file) */
/* ---------------------------------------------------------------------- */
#endif /* CBUILD_H */

#ifdef CBUILD_IMPLEMENTATION
#include <errno.h>
#include <limits.h>  // for PATH_MAX
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>   // _mkdir
#include <io.h>       // _access or _stat
#include <process.h>  // _beginthreadex, _spawn
#include <windows.h>  // Windows API for threads, etc.
#define stat _stat
#define unlink _unlink
#define rmdir _rmdir
#define getcwd _getcwd
#define PATH_MAX MAX_PATH
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096  // reasonable default for most systems
#endif

// --- Pretty-printing helpers (ANSI colors) ---
#ifndef _WIN32
#define CBUILD_COLOR_RESET "\033[0m"
#define CBUILD_COLOR_BOLD "\033[1m"
#define CBUILD_COLOR_GREEN "\033[32m"
#define CBUILD_COLOR_YELLOW "\033[33m"
#define CBUILD_COLOR_BLUE "\033[34m"
#define CBUILD_COLOR_MAGENTA "\033[35m"
#define CBUILD_COLOR_RED "\033[31m"
#else
#define CBUILD_COLOR_RESET ""
#define CBUILD_COLOR_BOLD ""
#define CBUILD_COLOR_GREEN ""
#define CBUILD_COLOR_YELLOW ""
#define CBUILD_COLOR_BLUE ""
#define CBUILD_COLOR_MAGENTA ""
#define CBUILD_COLOR_RED ""
#endif

static void cbuild_pretty_step(const char *label, const char *color,
                               const char *fmt, ...) {
    printf("%s%-10s%s ", color, label, CBUILD_COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

static void cbuild_pretty_status(int ok, const char *fmt, ...) {
    if (ok)
        printf("%s%s%s ", CBUILD_COLOR_GREEN, "✔", CBUILD_COLOR_RESET);
    else
        printf("%s%s%s ", CBUILD_COLOR_RED, "✖", CBUILD_COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

#define CBUILD_EXE_SUFFIX ".exe"

static int cbuild__needs_rebuild(const char *exe_path, const char **sources,
                                 int sources_count) {
    struct stat st_exe;
    if (stat(exe_path, &st_exe) != 0)
        return 1;
    for (int i = 0; i < sources_count; ++i) {
        struct stat st_src;
        if (stat(sources[i], &st_src) != 0)
            continue;
        if (st_src.st_mtime > st_exe.st_mtime)
            return 1;
    }
    return 0;
}

static void cbuild__exec_new_build(const char *exe_path, int argc,
                                   char **argv) {
#ifdef _WIN32
    _spawnv(_P_OVERLAY, exe_path, argv);
    exit(1);
#else
    execv(exe_path, argv);
    perror("execv");
    exit(1);
#endif
}

void cbuild_self_rebuild_if_needed(int argc, char **argv, const char **sources,
                                   int sources_count) {
    char exe_path[512];
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
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

    if (cbuild__needs_rebuild(exe_path, sources, sources_count)) {
        printf("cbuild: Detected changes, rebuilding build executable...\n");
        fflush(stdout);
        rename(exe_path, old_path);

#ifdef _WIN32
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cl /nologo /Fe:%s build.c /I. /Iinclude",
                 exe_path);
#else
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cc -o '%s' build.c -I. -Iinclude", exe_path);
#endif
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr, "cbuild: Self-rebuild failed!\n");
            exit(1);
        }
        cbuild__exec_new_build(exe_path, argc, argv);
    }
}

/* --- Data Structures for Build Targets, Commands, and Build Config --- */

typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_SHARED_LIB,
    TARGET_COMMAND
} cbuild_target_type;

struct cbuild_command {
    char *name;
    char *command_line;
    command_t **dependencies;
    int dep_count, dep_cap;
    int executed;  // internal: has this command been executed?
    int result;    // internal: result code after execution
};

/* Structure representing a build target (executable or library) */
struct cbuild_target {
    cbuild_target_type type;
    char *name;      // base name of target
    char **sources;  // array of source file paths
    int sources_count;
    int sources_cap;
    char **include_dirs;  // array of include directory paths
    int include_count;
    int include_cap;
    char **lib_dirs;  // library directories for linking
    int lib_dir_count;
    int lib_dir_cap;
    char **link_libs;  // external libraries to link (names or paths)
    int link_lib_count;
    int link_lib_cap;
    target_t *
        *dependencies;  // other targets this target depends on (to link against)
    int dep_count;
    int dep_cap;
    char *cflags;          // extra compile flags specific to this target
    char *ldflags;         // extra linker flags specific to this target
    char *output_file;     // path to final output (exe, .a, .dll/.so)
    char *obj_dir;         // directory for this target's object files (and .d files)
    command_t **commands;  // commands to run before building this target
    int cmd_count, cmd_cap;
    command_t **post_commands;  // commands to run after building this target
    int post_cmd_count, post_cmd_cap;
    char **defines; /* “FOO”   or “BAR=42” */
    int define_count;
    int define_cap;
};

/* Global list of targets */
static target_t **g_targets = NULL;
static int g_target_count = 0;
static int g_target_cap = 0;

/* Global list of commands */
static command_t **g_commands = NULL;
static int g_command_count = 0, g_command_cap = 0;

/* Global build settings */
static char *g_output_dir =
    NULL;  // base output directory for build files (default "build")
static int g_parallel_jobs =
    0;                     // number of parallel compile jobs (0 means not set yet)
static char *g_cc = NULL;  // C compiler command (gcc, clang, cl, etc.)
static char *g_ar = NULL;  // static library archiver command (ar or lib)
static char *g_ld =
    NULL;  // linker command if needed (usually same as compiler for exec/shared)
static char *g_global_cflags =
    NULL;                              // global compiler flags (like debug symbols, optimizations)
static char *g_global_ldflags = NULL;  // global linker flags

/* --- global macro definitions (apply to every target) -------------- */
static char **g_global_defines = NULL;
static int g_global_def_count = 0;
static int g_global_def_cap = 0;

/* Global list of subcommands */
typedef struct cbuild_subcommand {
    char *name;
    target_t *target;
    char *command_line;
    cbuild_subcommand_callback callback;
    void *user_data;
} cbuild_subcommand_t;

static cbuild_subcommand_t **g_subcommands = NULL;
static int g_subcommand_count = 0, g_subcommand_cap = 0;

/* --- compile_commands.json support --- */
static int g_generate_compile_commands = 0;
typedef struct {
    char *directory;
    char *command;
    char *file;
} compile_commands_entry_t;
static compile_commands_entry_t *g_cc_entries = NULL;
static int g_cc_count = 0, g_cc_cap = 0;

// Public API implementation
void cbuild_enable_compile_commands(int enabled) {
    g_generate_compile_commands = enabled;
}

/* Forward declarations of internal utility functions */
static void cbuild_init();  // initialize defaults
static target_t *cbuild_create_target(const char *name,
                                       cbuild_target_type type);
static void ensure_capacity_charpp(char ***arr, int *count, int *capacity);
static void append_str(char **dst, const char *src);
static void append_format(char **dst, const char *fmt, ...);
static int ensure_dir_exists(const char *path);
static int run_command(const char *cmd, int capture_out,
                        char **captured_output);
static int compile_source(const char *src_file, const char *obj_file,
                           const char *dep_file, target_t *t);
static void collect_compile_commands_for_target(target_t *t);
static int need_recompile(const char *src_file, const char *obj_file,
                           const char *dep_file);
static int link_target(target_t *t);
static void remove_file(const char *path);
static void remove_dir_recursive(const char *path);
static void schedule_compile_jobs(target_t *t, int *error_flag);
static void build_target(target_t *t, int *error_flag);
static int cbuild_match_wildcard(const char *pattern, const char *string);
static int cbuild_expand_wildcard(const char *pattern, char ***files,
                                   int *file_count);
static int cbuild_expand_wildcard_recursive(const char *dir_path,
                                             const char *pattern, char ***files,
                                             int *file_count, int *capacity);
static int cbuild_add_to_file_list(char ***files, int *file_count,
                                    int *capacity, const char *path);

/* Structures and funcs for thread pool (for parallel compilation) */
#ifdef _WIN32
static HANDLE *g_thread_handles = NULL;
static int g_thread_count = 0;
static CRITICAL_SECTION g_job_mutex;
#else
static pthread_t *g_thread_ids = NULL;
static int g_thread_count = 0;
static pthread_mutex_t g_job_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int g_next_job_index = 0;  // index of next compile job to pick
typedef struct {
    target_t *target;
    int index;
} compile_job_t;
static compile_job_t *g_compile_jobs = NULL;
static int g_compile_job_count = 0;

/* Thread worker function prototype */
#ifdef _WIN32
static DWORD WINAPI compile_thread_func(LPVOID param);
#else
static void *compile_thread_func(void *param);
#endif

/* Helper macro to get max of two values */
#define CBUILD_MAX(a, b) ((a) > (b) ? (a) : (b))

/* --- Subproject types and helpers --- */
typedef struct cbuild_subproject_target {
    char *name;                          // logical name (e.g. "zlib")
    char *type;                          // "static_lib", "shared_lib", "executable"
    char *output_path;                   // relative to subproject directory
    struct cbuild_target *proxy_target;  // created on demand
} cbuild_subproject_target_t;

struct cbuild_subproject {
    char *alias;
    char *directory;
    char *cbuild_exe;
    command_t
        *build_cmd;  // command_t to build the subproject (runs cbuild --manifest)
    int manifest_loaded;
    cbuild_subproject_target_t *targets;
    int target_count, target_cap;
};

static subproject_t **g_subprojects = NULL;
static int g_subproject_count = 0, g_subproject_cap = 0;

/* --- Implementation: Utility Functions --- */

// Public Helper: check if a file exists
int cbuild_file_exists(const char *path) {
    if (!path || !*path)
        return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Public Helper: check if a directory exists
int cbuild_dir_exists(const char *path) {
    if (!path || !*path)
        return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Public Helper: remove a file
int cbuild_remove_file(const char *path) {
    if (!path || !*path)
        return -1;
    if (!cbuild_file_exists(path))
        return 0;
    return unlink(path);
}

// Public Helper: remove a directory recursively
int cbuild_remove_dir(const char *path) {
    if (!path || !*path)
        return -1;
    if (!cbuild_dir_exists(path))
        return 0;
#ifdef _WIN32
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", path);
    return system(cmd);
#else
    DIR *dir;
    struct dirent *entry;
    char full_path[PATH_MAX];
    if (!(dir = opendir(path)))
        return -1;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (cbuild_remove_dir(full_path) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            if (cbuild_remove_file(full_path) != 0) {
                closedir(dir);
                return -1;
            }
        }
    }
    closedir(dir);
    return rmdir(path);
#endif
}

// Public Helper: get the current working directory
int cbuild_get_cwd(char *buf, long size) {
    if (!buf || size <= 0)
        return -1;
    return getcwd(buf, size) ? 0 : -1;
}

/**
 * Simple wildcard pattern matching function.
 *
 * @param pattern The pattern to match against (supports * and ? wildcards)
 * @param string  The string to check
 * @return        1 if the string matches the pattern, 0 otherwise
 *
 * Supports:
 *   - * (match 0 or more characters)
 *   - ? (match any single character)
 */
static int cbuild_match_wildcard(const char *pattern, const char *string) {
    if (!pattern || !string)
        return 0;

    // End of pattern reached
    if (*pattern == '\0')
        return *string == '\0';

    // Handle wildcard *
    if (*pattern == '*') {
        // Skip consecutive * characters (but not ** which has special meaning)
        if (*(pattern + 1) == '*' && *(pattern + 2) != '*')
            pattern++;

        // * at the end of the pattern matches anything
        if (*(pattern + 1) == '\0')
            return 1;

        // Try to match the rest of the pattern with every substring
        while (*string) {
            if (cbuild_match_wildcard(pattern + 1, string))
                return 1;
            string++;
        }
        return cbuild_match_wildcard(pattern + 1, string);
    }

    // Handle ? or exact match
    if (*pattern == '?' || *pattern == *string) {
        return cbuild_match_wildcard(pattern + 1, string + 1);
    }

    return 0;
}

/**
 * Helper functions for recursive pattern matching and file expansion
 */

/**
 * Expands a wildcard pattern to a list of matching files.
 *
 * @param pattern    The pattern to expand (e.g. "src/*.c" or "src/**'/*.c")
 * @param files      Output pointer to array of strings that will be filled with
 * matching files
 * @param file_count Output pointer to an integer that will be set to the number
 * of files found
 * @return           0 on success or -1 on error
 *
 * Supports:
 *   - Basic wildcards: "src/*.c" matches all .c files in src directory
 *   - Recursive wildcards: "src/**'/*.c" matches all .c files in src and all
 * subdirectories
 */
static int cbuild_expand_wildcard(const char *pattern, char ***files,
                                  int *file_count) {
    char dir_path[PATH_MAX] = { 0 };
    char base_pattern[PATH_MAX] = { 0 };
    int capacity = 32;  // Initial capacity

    if (!pattern || !files || !file_count)
        return -1;

    *files = NULL;
    *file_count = 0;

    // Extract directory part and filename pattern
    const char *last_slash = strrchr(pattern, '/');
    const char *last_backslash = strrchr(pattern, '\\');
    const char *separator = last_slash ? last_slash : last_backslash;

    if (!separator) {
        // No directory part, use current directory
        strcpy(dir_path, ".");
        strcpy(base_pattern, pattern);
    } else {
        // Extract directory part
        size_t dir_len = separator - pattern;
        strncpy(dir_path, pattern, dir_len);
        dir_path[dir_len] = '\0';

        // Extract base pattern
        strcpy(base_pattern, separator + 1);
    }

    // Allocate memory for file list
    *files = (char **)malloc(capacity * sizeof(char *));
    if (!*files)
        return -1;

    // Begin recursive expansion from the base directory
    int result = cbuild_expand_wildcard_recursive(dir_path, base_pattern, files,
                                                  file_count, &capacity);

    if (result != 0 && *files) {
        // Clean up on error
        for (int i = 0; i < *file_count; i++) {
            free((*files)[i]);
        }
        free(*files);
        *files = NULL;
        *file_count = 0;
    }

    return result;
}

/**
 * Helper function to add a file to the result list.
 * Handles memory allocation and growth of the file list array.
 *
 * @param files      Pointer to the array of file paths
 * @param file_count Pointer to the current count of files
 * @param capacity   Pointer to the current capacity of the array
 * @param path       The file path to add to the list
 * @return           0 on success, -1 on failure
 */
static int cbuild_add_to_file_list(char ***files, int *file_count,
                                   int *capacity, const char *path) {
    // Grow array if needed
    if (*file_count >= *capacity) {
        *capacity *= 2;
        char **new_files = (char **)realloc(*files, *capacity * sizeof(char *));
        if (!new_files) {
            return -1;
        }
        *files = new_files;
    }

    // Add path to the result
    (*files)[*file_count] = strdup(path);
    if (!(*files)[*file_count]) {
        return -1;
    }
    (*file_count)++;

    return 0;
}

/**
 * Recursively expands wildcard patterns and handles ** for directory traversal.
 * This is the workhorse function that handles directory traversal and pattern
 * matching.
 *
 * @param dir_path   The directory to search in
 * @param pattern    The pattern to match against files (can include
 * subdirectory parts)
 * @param files      Pointer to the array of file paths
 * @param file_count Pointer to the current count of files
 * @param capacity   Pointer to the current capacity of the array
 * @return           0 on success, -1 on failure
 */
static int cbuild_expand_wildcard_recursive(const char *dir_path,
                                            const char *pattern, char ***files,
                                            int *file_count, int *capacity) {
    DIR *dir;
    struct dirent *entry;

    // Handle special case for ** pattern
    int recursive_search = 0;
    char next_pattern[PATH_MAX] = { 0 };

    if (strncmp(pattern, "**", 2) == 0) {
        recursive_search = 1;

        // Extract the rest of the pattern after **
        if (pattern[2] == '/' || pattern[2] == '\\') {
            strcpy(next_pattern, pattern + 3);
        } else {
            strcpy(next_pattern, pattern + 2);
        }
    }

    // Open directory
    dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    // Read directory entries and check for matches
    while ((entry = readdir(dir))) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Construct the full path
        char full_path[PATH_MAX];
        if (strcmp(dir_path, ".") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        }

        // Check if this is a directory
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (recursive_search) {
                // For ** pattern, process files in this directory with the remaining
                // pattern
                if (next_pattern[0]) {
                    cbuild_expand_wildcard_recursive(full_path, next_pattern, files,
                                                     file_count, capacity);
                }

                // Also search subdirectories with the original ** pattern
                cbuild_expand_wildcard_recursive(full_path, pattern, files, file_count,
                                                 capacity);
            } else if (strchr(pattern, '/') || strchr(pattern, '\\')) {
                // Handle directory/pattern format by traversing to subdirectory
                const char *slash = strchr(pattern, '/');
                if (!slash)
                    slash = strchr(pattern, '\\');

                char subdir_pattern[PATH_MAX] = { 0 };
                strncpy(subdir_pattern, pattern, slash - pattern);
                subdir_pattern[slash - pattern] = '\0';

                if (cbuild_match_wildcard(subdir_pattern, entry->d_name)) {
                    cbuild_expand_wildcard_recursive(full_path, slash + 1, files,
                                                     file_count, capacity);
                }
            }
        } else if (!recursive_search &&
                   cbuild_match_wildcard(pattern, entry->d_name)) {
            // Check if this entry matches the pattern (for files)
            if (cbuild_add_to_file_list(files, file_count, capacity, full_path) !=
                0) {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

// Helper: join two paths with a slash
static char *cbuild__join_path(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int need_slash = (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\');
    char *out = (char *)malloc(la + lb + 2);
    strcpy(out, a);
    if (need_slash)
        strcat(out, "/");
    strcat(out, b);
    return out;
}

// Helper: trim whitespace
static void cbuild__trim(char *s) {
    char *end;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++;
    end = s + strlen(s) - 1;
    while (end > s &&
           (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = 0;
}

// Helper: parse manifest by calling subproject's cbuild with --manifest flag
static void cbuild__parse_manifest(subproject_t *sub) {
    if (sub->manifest_loaded)
        return;

    // Construct the command to run the subproject's cbuild with --manifest
    char *cmd = NULL;
#ifdef _WIN32
    append_format(&cmd, "cd /d \"%s\" && \"%s\" --manifest", sub->directory,
                  sub->cbuild_exe);
#else
    append_format(&cmd, "cd '%s' && '%s' --manifest", sub->directory,
                  sub->cbuild_exe);
#endif

    // Execute the command and capture its output
    char *output = NULL;
    int result = run_command(cmd, 1, &output);
    free(cmd);

    if (result != 0 || !output) {
        fprintf(stderr, "cbuild: Failed to get manifest from subproject '%s'\n",
                sub->alias);
        if (output)
            free(output);
        return;
    }

    // Parse the output line by line
    char *saveptr = NULL;
    char *line = strtok_r(output, "\r\n", &saveptr);

    while (line) {
        cbuild__trim(line);
        if (!line[0] || line[0] == '#') {
            line = strtok_r(NULL, "\r\n", &saveptr);
            continue;
        }

        // Format: TYPE NAME PATH
        char *line_copy = strdup(line);
        char *type = strtok(line_copy, " \t");
        char *name = strtok(NULL, " \t");
        char *path = strtok(NULL, "\r\n");

        if (type && name && path) {
            cbuild__trim(path);
            // Add to sub->targets
            if (sub->target_count + 1 > sub->target_cap) {
                sub->target_cap = sub->target_cap ? sub->target_cap * 2 : 4;
                sub->targets = realloc(
                    sub->targets, sub->target_cap * sizeof(cbuild_subproject_target_t));
            }
            sub->targets[sub->target_count].name = strdup(name);
            sub->targets[sub->target_count].type = strdup(type);
            sub->targets[sub->target_count].output_path = strdup(path);
            sub->targets[sub->target_count].proxy_target = NULL;
            sub->target_count++;
        }

        free(line_copy);
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    free(output);
    sub->manifest_loaded = 1;
}

// Helper: find target in manifest
static cbuild_subproject_target_t *
cbuild__find_subproject_target(subproject_t *sub, const char *tgt_name) {
    cbuild__parse_manifest(sub);
    for (int i = 0; i < sub->target_count; ++i) {
        if (strcmp(sub->targets[i].name, tgt_name) == 0) {
            return &sub->targets[i];
        }
    }
    return NULL;
}

/* ensure_capacity_charpp: ensure char** array has room for one more element
 * (expand if needed) */
static void ensure_capacity_charpp(char ***arr, int *count, int *capacity) {
    if (*count + 1 > *capacity) {
        int newcap = *capacity ? *capacity * 2 : 4;
        char **newarr = (char **)realloc(*arr, newcap * sizeof(char *));
        if (!newarr) {
            fprintf(stderr, "cbuild: Out of memory\n");
            exit(1);
        }
        *arr = newarr;
        *capacity = newcap;
    }
}

/* append_str: append a copy of string src onto dynamic string *dst
 * (reallocating *dst) */
static void append_str(char **dst, const char *src) {
    if (!src)
        return;
    size_t src_len = strlen(src);
    size_t old_len = *dst ? strlen(*dst) : 0;
    *dst = (char *)realloc(*dst, old_len + src_len + 1);
    if (!*dst) {
        fprintf(stderr, "cbuild: Out of memory\n");
        exit(1);
    }
    strcpy(*dst + old_len, src);
}

/* append_format: append formatted text to a dynamic string (like a simple
 * asprintf accumulation) */
static void append_format(char **dst, const char *fmt, ...) {
    va_list args;
    va_list args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int add_len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (add_len < 0) {
        va_end(args);
        return;
    }
    size_t old_len = *dst ? strlen(*dst) : 0;
    *dst = (char *)realloc(*dst, old_len + add_len + 1);
    if (!*dst) {
        fprintf(stderr, "cbuild: Out of memory\n");
        exit(1);
    }
    vsnprintf(*dst + old_len, add_len + 1, fmt, args);
    va_end(args);
}

/* ensure_dir_exists: create a directory (and parent directories) if not
   present. Returns 0 on success, -1 on failure. */
static int ensure_dir_exists(const char *path) {
    if (!path || !*path)
        return 0;
    // We will create intermediate dirs one by one.
    char temp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        return -1;  // path too long
    }
    strcpy(temp, path);
    // Remove trailing slash or backslash if any
    if (temp[len - 1] == '/' || temp[len - 1] == '\\') {
        temp[len - 1] = '\0';
    }
    for (char *p = temp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            // Temporarily truncate at this subdir
            *p = '\0';
#ifdef _WIN32
            if (_mkdir(temp) != 0) {
                if (errno != EEXIST) {
                    *p = '/';
                    return -1;
                }
            }
#else
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return -1;
            }
#endif
            *p = '/';  // restore separator
        }
    }
    // Create final directory
#ifdef _WIN32
    if (_mkdir(temp) != 0) {
        if (errno != EEXIST)
            return -1;
    }
#else
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
#endif
    return 0;
}

/* run_command: Runs a shell command. If capture_out is true, captures stdout in
   *captured_output (must be freed by caller). Returns process exit code (0 for
   success). */
static int run_command(const char *cmd, int capture_out,
                       char **captured_output) {
    if (capture_out) {
        // Open pipe to capture output
#ifdef _WIN32
        // Use _popen on Windows
        FILE *pipe = _popen(cmd, "r");
#else
        FILE *pipe = popen(cmd, "r");
#endif
        if (!pipe) {
            return -1;
        }
        char buffer[256];
        size_t out_len = 0;
        *captured_output = NULL;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            size_t chunk = strlen(buffer);
            *captured_output = (char *)realloc(*captured_output, out_len + chunk + 1);
            if (!*captured_output) {
                fprintf(stderr, "cbuild: Out of memory capturing command output\n");
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
        // Normalize exit code (on Windows, _pclose returns -1 if error launching)
        if (exitCode == -1) {
            return -1;
        }
        // On Windows, exitCode is the result of cmd << 8 (conventional), so 0 means
        // success. We'll just return as is, since for our usage 0 indicates
        // success.
        return exitCode;
    } else {
        // Not capturing output: use system() to execute (prints output directly).
        int ret = system(cmd);
        // system returns encoded status; we assume 0 means success on both Windows
        // and Unix.
        return ret;
    }
}

/* need_recompile: Checks timestamps of source, object, and included headers to
   decide if recompilation is needed. Returns 1 if the source (or its headers)
   is newer than object (or object missing), otherwise 0. */
static int need_recompile(const char *src_file, const char *obj_file,
                          const char *dep_file) {
    struct stat st_src, st_obj;
    if (stat(src_file, &st_src) != 0) {
        return 1;
    }
    if (stat(obj_file, &st_obj) != 0) {
        return 1;
    }
    if (st_src.st_mtime > st_obj.st_mtime) {
        return 1;
    }
    return 0;
}

/* compile_source: Compile a single source file to object file (and produce dep
   file). Returns 0 on success, non-zero on failure. */
static int compile_source(const char *src_file, const char *obj_file,
                          const char *dep_file, target_t *t) {
    ensure_dir_exists(t->obj_dir);
    char *cmd = NULL;
    append_format(&cmd, "\"%s\" ", g_cc);
#ifdef _WIN32
    append_format(&cmd, "/c /nologo /Fo\"%s\" ", obj_file);
    append_str(&cmd, "/showIncludes ");
#else
    append_format(&cmd, "-c -o \"%s\" ", obj_file);
#endif
    if (t->cflags && strlen(t->cflags) > 0) {
        append_format(&cmd, "%s ", t->cflags);
    } else if (g_global_cflags) {
        append_format(&cmd, "%s ", g_global_cflags);
    }
    for (int i = 0; i < t->include_count; ++i) {
        const char *inc = t->include_dirs[i];
#ifdef _WIN32
        append_format(&cmd, "/I \"%s\" ", inc);
#else
        append_format(&cmd, "-I\"%s\" ", inc);
#endif
    }

    /* --- NEW: global + per‑target defines -------------------------------- */
    for (int i = 0; i < g_global_def_count; ++i) {
#ifdef _WIN32
        append_format(&cmd, "/D%s ", g_global_defines[i]);
#else
        append_format(&cmd, "-D%s ", g_global_defines[i]);
#endif
    }

    for (int i = 0; i < t->define_count; ++i) {
#ifdef _WIN32
        append_format(&cmd, "/D%s ", t->defines[i]);
#else
        append_format(&cmd, "-D%s ", t->defines[i]);
#endif
    }

    append_format(&cmd, "\"%s\"", src_file);

    int result;
    char *output = NULL;
#ifdef _WIN32
    result = run_command(cmd, 1, &output);
    if (output) {
        FILE *df = fopen(dep_file, "w");
        if (df) {
            fprintf(df, "%s: %s", obj_file, src_file);
            char *saveptr = NULL;
            char *line = strtok_r(output, "\r\n", &saveptr);
            while (line) {
                const char *tag = "Note: including file:";
                char *pos = strstr(line, tag);
                if (pos) {
                    pos += strlen(tag);
                    while (*pos == ' ' || *pos == '\t')
                        pos++;
                    if (*pos) {
                        fprintf(df, " \\\n  %s", pos);
                    }
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
            fprintf(df, "\n");
            fclose(df);
        }
        if (result != 0) {
            fwrite(output, 1, strlen(output), stderr);
        }
        free(output);
    }
#else
    result = run_command(cmd, 1, &output);
    if (output && result != 0) {
        fwrite(output, 1, strlen(output), stderr);
    }
    if (output)
        free(output);
#endif
    free(cmd);
    if (result != 0) {
        fprintf(stderr, "cbuild: Compilation failed for %s\n", src_file);
    }
    return result;
}

/* Collect compile commands for all sources of a target (for compile_commands.json) */
static void collect_compile_commands_for_target(target_t *t) {
    if (!g_generate_compile_commands)
        return;
    for (int i = 0; i < t->sources_count; ++i) {
        const char *src_file = t->sources[i];
        const char *slash = strrchr(src_file, '/');
        const char *base = slash ? slash + 1 : src_file;
        char *dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s.o", t->obj_dir, (int)len, base);

        char *cmd = NULL;
        append_format(&cmd, "\"%s\" ", g_cc);
    #ifdef _WIN32
        append_format(&cmd, "/c /nologo /Fo\"%s\" ", objname);
        append_str(&cmd, "/showIncludes ");
    #else
        append_format(&cmd, "-c -o \"%s\" ", objname);
    #endif
        if (t->cflags && strlen(t->cflags) > 0) {
            append_format(&cmd, "%s ", t->cflags);
        } else if (g_global_cflags) {
            append_format(&cmd, "%s ", g_global_cflags);
        }
        for (int j = 0; j < t->include_count; ++j) {
            const char *inc = t->include_dirs[j];
        #ifdef _WIN32
            append_format(&cmd, "/I \"%s\" ", inc);
        #else
            append_format(&cmd, "-I\"%s\" ", inc);
        #endif
        }
        for (int j = 0; j < g_global_def_count; ++j) {
        #ifdef _WIN32
            append_format(&cmd, "/D%s ", g_global_defines[j]);
        #else
            append_format(&cmd, "-D%s ", g_global_defines[j]);
        #endif
        }
        for (int j = 0; j < t->define_count; ++j) {
        #ifdef _WIN32
            append_format(&cmd, "/D%s ", t->defines[j]);
        #else
            append_format(&cmd, "-D%s ", t->defines[j]);
        #endif
        }
        append_format(&cmd, "\"%s\"", src_file);

        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            if (g_cc_count + 1 > g_cc_cap) {
                g_cc_cap = g_cc_cap ? g_cc_cap * 2 : 4;
                g_cc_entries = realloc(g_cc_entries, g_cc_cap * sizeof(*g_cc_entries));
            }
            g_cc_entries[g_cc_count].directory = strdup(cwd);
            g_cc_entries[g_cc_count].command = strdup(cmd);
            g_cc_entries[g_cc_count].file = strdup(src_file);
            g_cc_count++;
        }
        free(cmd);
    }
}

/* --- Implementation: Command API --- */

command_t *cbuild_command(const char *name, const char *command_line) {
    command_t *cmd = (command_t *)calloc(1, sizeof(command_t));
    cmd->name = strdup(name);
    cmd->command_line = strdup(command_line);
    // Add to global command list
    ensure_capacity_charpp((char ***)&g_commands, &g_command_count,
                           &g_command_cap);
    g_commands[g_command_count++] = cmd;
    return cmd;
}

void cbuild_target_add_command(target_t *target, command_t *cmd) {
    if (!target || !cmd)
        return;
    ensure_capacity_charpp((char ***)&target->commands, &target->cmd_count,
                           &target->cmd_cap);
    target->commands[target->cmd_count++] = cmd;
}

void cbuild_target_add_cflags(target_t *target, const char *cflags) {
    if (!target || !cflags)
        return;

    // If target doesn't have cflags yet, initialize with the provided flags
    if (!target->cflags) {
        target->cflags = strdup(cflags);
    } else {
        // Otherwise append the new flags with a space separator
        char *new_cflags =
            (char *)malloc(strlen(target->cflags) + strlen(cflags) + 2);
        sprintf(new_cflags, "%s %s", target->cflags, cflags);
        free(target->cflags);
        target->cflags = new_cflags;
    }
}

void cbuild_target_add_post_command(target_t *target, command_t *cmd) {
    if (!target || !cmd)
        return;
    ensure_capacity_charpp((char ***)&target->post_commands,
                           &target->post_cmd_count, &target->post_cmd_cap);
    target->post_commands[target->post_cmd_count++] = cmd;
}

void cbuild_command_add_dependency(command_t *cmd, command_t *dependency) {
    if (!cmd || !dependency)
        return;
    ensure_capacity_charpp((char ***)&cmd->dependencies, &cmd->dep_count,
                           &cmd->dep_cap);
    cmd->dependencies[cmd->dep_count++] = dependency;
}

int cbuild_run_command(command_t *cmd) {
    if (!cmd)
        return -1;
    // Run dependencies first
    for (int i = 0; i < cmd->dep_count; ++i) {
        int rc = cbuild_run_command(cmd->dependencies[i]);
        if (rc != 0)
            return rc;
    }
    if (cmd->executed)
        return cmd->result;
    cbuild_pretty_step("COMMAND", CBUILD_COLOR_MAGENTA, "%s", cmd->name);
    int rc = run_command(cmd->command_line, 0, NULL);
    cmd->executed = 1;
    cmd->result = rc;
    if (rc != 0) {
        cbuild_pretty_status(0, "Command failed: %s", cmd->name);
    }
    return rc;
}

/* --- Implementation: Subcommand API --- */

void cbuild_register_subcommand(const char *name, target_t *target,
                                const char *command_line,
                                cbuild_subcommand_callback callback,
                                void *user_data) {
    cbuild_subcommand_t *scmd =
        (cbuild_subcommand_t *)calloc(1, sizeof(cbuild_subcommand_t));
    scmd->name = strdup(name);
    scmd->target = target;
    scmd->command_line = command_line ? strdup(command_line) : NULL;
    scmd->callback = callback;
    scmd->user_data = user_data;
    ensure_capacity_charpp((char ***)&g_subcommands, &g_subcommand_count,
                           &g_subcommand_cap);
    g_subcommands[g_subcommand_count++] = scmd;
}

/* --- Subproject API: Add a subproject to the build system --- */

subproject_t *cbuild_add_subproject(const char *alias, const char *directory,
                                    const char *cbuild_exe) {
    subproject_t *sub = (subproject_t *)calloc(1, sizeof(subproject_t));
    sub->alias = strdup(alias);
    sub->directory = strdup(directory);
    sub->cbuild_exe = strdup(cbuild_exe);

    // Build command to build the subproject's targets
    char *cmdline = NULL;
#ifdef _WIN32
    append_format(&cmdline, "cd /d \"%s\" && \"%s\"", directory, cbuild_exe);
#else
    append_format(&cmdline, "cd '%s' && '%s'", directory, cbuild_exe);
#endif
    char build_cmd_name[256];
    snprintf(build_cmd_name, sizeof(build_cmd_name), "build subproject %s",
             alias);
    sub->build_cmd = cbuild_command(build_cmd_name, cmdline);
    free(cmdline);

    // Register in global subproject list
    if (g_subproject_count + 1 > g_subproject_cap) {
        g_subproject_cap = g_subproject_cap ? g_subproject_cap * 2 : 4;
        g_subprojects =
            realloc(g_subprojects, g_subproject_cap * sizeof(subproject_t *));
    }
    g_subprojects[g_subproject_count++] = sub;
    return sub;
}

target_t *cbuild_subproject_get_target(subproject_t *sub,
                                       const char *tgt_name) {
    if (!sub)
        return NULL;
    cbuild_subproject_target_t *stgt =
        cbuild__find_subproject_target(sub, tgt_name);
    if (!stgt) {
        fprintf(stderr, "cbuild: Subproject '%s' has no target named '%s'\n",
                sub->alias, tgt_name);
        return NULL;
    }
    if (stgt->proxy_target)
        return stgt->proxy_target;

    // Create a proxy target_t
    cbuild_target_type type;
    if (strcmp(stgt->type, "static_lib") == 0) {
        type = TARGET_STATIC_LIB;
    } else if (strcmp(stgt->type, "shared_lib") == 0) {
        type = TARGET_SHARED_LIB;
    } else if (strcmp(stgt->type, "executable") == 0) {
        type = TARGET_EXECUTABLE;
    } else {
        fprintf(stderr, "cbuild: Unknown subproject target type: %s\n", stgt->type);
        return NULL;
    }

    // Name the proxy as "<alias>_<tgt_name>"
    char proxy_name[256];
    snprintf(proxy_name, sizeof(proxy_name), "%s_%s", sub->alias, stgt->name);
    target_t *proxy = (target_t *)calloc(1, sizeof(target_t));
    proxy->type = type;
    proxy->name = strdup(proxy_name);

    // Output file is subproject_dir + "/" + output_path
    proxy->output_file = cbuild__join_path(sub->directory, stgt->output_path);
    proxy->obj_dir = NULL;  // not used

    // Add the subproject build command as a dependency
    proxy->commands = NULL;
    proxy->cmd_count = proxy->cmd_cap = 0;
    cbuild_target_add_command(proxy, sub->build_cmd);

    // Add to global targets list so it can be linked
    ensure_capacity_charpp((char ***)&g_targets, &g_target_count, &g_target_cap);
    g_targets[g_target_count++] = proxy;

    stgt->proxy_target = proxy;
    return proxy;
}

/* --- End Subproject API Implementation --- */

/* --- Implementation: Public API Functions --- */

target_t *cbuild_executable(const char *name) {
    return cbuild_create_target(name, TARGET_EXECUTABLE);
}

target_t *cbuild_static_library(const char *name) {
    return cbuild_create_target(name, TARGET_STATIC_LIB);
}

target_t *cbuild_shared_library(const char *name) {
    return cbuild_create_target(name, TARGET_SHARED_LIB);
}

void cbuild_add_source(target_t *target, const char *source_file) {
    if (strchr(source_file, '*') || strchr(source_file, '?')) {
        // This is a wildcard pattern
        char **expanded_files = NULL;
        int file_count = 0;

        if (cbuild_expand_wildcard(source_file, &expanded_files, &file_count) ==
                0 &&
            file_count > 0) {
            for (int i = 0; i < file_count; i++) {
                ensure_capacity_charpp(&target->sources, &target->sources_count,
                                       &target->sources_cap);
                target->sources[target->sources_count++] =
                    expanded_files[i];  // Transfer ownership
            }
            free(expanded_files);  // Just free the array, not the strings
        } else {
            fprintf(stderr, "Warning: No files found matching pattern '%s'\n",
                    source_file);
        }
    } else {
        // Regular file path
        ensure_capacity_charpp(&target->sources, &target->sources_count,
                               &target->sources_cap);
        target->sources[target->sources_count++] = strdup(source_file);
    }
}

void cbuild_add_include_dir(target_t *target, const char *include_dir) {
    if (strchr(include_dir, '*') || strchr(include_dir, '?')) {
        // This is a wildcard pattern
        char **expanded_dirs = NULL;
        int dir_count = 0;

        if (cbuild_expand_wildcard(include_dir, &expanded_dirs, &dir_count) == 0 &&
            dir_count > 0) {
            for (int i = 0; i < dir_count; i++) {
                if (cbuild_dir_exists(expanded_dirs[i])) {
                    ensure_capacity_charpp(&target->include_dirs, &target->include_count,
                                           &target->include_cap);
                    target->include_dirs[target->include_count++] =
                        expanded_dirs[i];  // Transfer ownership
                } else {
                    free(expanded_dirs[i]);  // Not a directory, free it
                }
            }
            free(expanded_dirs);  // Just free the array
        } else {
            fprintf(stderr, "Warning: No directories found matching pattern '%s'\n",
                    include_dir);
        }
    } else {
        // Regular directory path
        ensure_capacity_charpp(&target->include_dirs, &target->include_count,
                               &target->include_cap);
        target->include_dirs[target->include_count++] = strdup(include_dir);
    }
}

void cbuild_add_library_dir(target_t *target, const char *lib_dir) {
    if (strchr(lib_dir, '*') || strchr(lib_dir, '?')) {
        // This is a wildcard pattern
        char **expanded_dirs = NULL;
        int dir_count = 0;

        if (cbuild_expand_wildcard(lib_dir, &expanded_dirs, &dir_count) == 0 &&
            dir_count > 0) {
            for (int i = 0; i < dir_count; i++) {
                if (cbuild_dir_exists(expanded_dirs[i])) {
                    ensure_capacity_charpp(&target->lib_dirs, &target->lib_dir_count,
                                           &target->lib_dir_cap);
                    target->lib_dirs[target->lib_dir_count++] =
                        expanded_dirs[i];  // Transfer ownership
                } else {
                    free(expanded_dirs[i]);  // Not a directory, free it
                }
            }
            free(expanded_dirs);  // Just free the array
        } else {
            fprintf(stderr, "Warning: No directories found matching pattern '%s'\n",
                    lib_dir);
        }
    } else {
        // Regular directory path
        ensure_capacity_charpp(&target->lib_dirs, &target->lib_dir_count,
                               &target->lib_dir_cap);
        target->lib_dirs[target->lib_dir_count++] = strdup(lib_dir);
    }
}

void cbuild_add_link_library(target_t *target, const char *lib) {
    if (strchr(lib, '*') || strchr(lib, '?')) {
        // This is a wildcard pattern
        char **expanded_files = NULL;
        int file_count = 0;

        if (cbuild_expand_wildcard(lib, &expanded_files, &file_count) == 0 &&
            file_count > 0) {
            for (int i = 0; i < file_count; i++) {
                ensure_capacity_charpp(&target->link_libs, &target->link_lib_count,
                                       &target->link_lib_cap);
                target->link_libs[target->link_lib_count++] =
                    expanded_files[i];  // Transfer ownership
            }
            free(expanded_files);  // Just free the array
        } else {
            fprintf(stderr, "Warning: No libraries found matching pattern '%s'\n",
                    lib);
        }
    } else {
        // Regular library path
        ensure_capacity_charpp(&target->link_libs, &target->link_lib_count,
                               &target->link_lib_cap);
        target->link_libs[target->link_lib_count++] = strdup(lib);
    }
}

void cbuild_target_link_library(target_t *dependant, target_t *dependency) {
    if (dependency) {
        ensure_capacity_charpp((char ***)&dependant->dependencies,
                               &dependant->dep_count, &dependant->dep_cap);
        dependant->dependencies[dependant->dep_count++] = dependency;
    }
}

void cbuild_set_output_dir(const char *dir) {
    if (g_output_dir) {
        free(g_output_dir);
    }
    g_output_dir = strdup(dir);
}

void cbuild_set_parallelism(int jobs_count) {
    g_parallel_jobs = jobs_count;
}

void cbuild_set_compiler(const char *compiler_exe) {
    if (g_cc)
        free(g_cc);
    g_cc = strdup(compiler_exe);
    if (strstr(compiler_exe, "cl") != NULL &&
        strstr(compiler_exe, "clang") == NULL) {
        if (g_ar)
            free(g_ar);
        g_ar = strdup("lib");
    } else if (strstr(compiler_exe, "cl") == NULL) {
        if (g_ar)
            free(g_ar);
        g_ar = strdup("ar");
    }
}

void cbuild_add_global_cflags(const char *flags) {
    append_format(&g_global_cflags, "%s ", flags);
}

void cbuild_add_global_ldflags(const char *flags) {
    append_format(&g_global_ldflags, "%s ", flags);
}

/* ---------- Implementation: Pre‑processor define API ------------------- */

static void cbuild__add_define_to_list(char ***arr,
                                       int *count,
                                       int *cap,
                                       const char *macro,
                                       const char *value_optional) {
    char *entry = NULL;
    if (value_optional)
        append_format(&entry, "%s=%s", macro, value_optional);
    else
        entry = strdup(macro);

    ensure_capacity_charpp(arr, count, cap);
    (*arr)[(*count)++] = entry;
}

/* Per‑target convenience ------------------------------------------------- */

void cbuild_add_define(target_t *t, const char *macro) {
    if (t && macro)
        cbuild__add_define_to_list(&t->defines,
                                   &t->define_count,
                                   &t->define_cap,
                                   macro, NULL);
}

void cbuild_add_define_val(target_t *t, const char *macro, const char *val) {
    if (t && macro)
        cbuild__add_define_to_list(&t->defines,
                                   &t->define_count,
                                   &t->define_cap,
                                   macro, val);
}

void cbuild_set_flag(target_t *t, const char *flag, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", !!value);
    cbuild_add_define_val(t, flag, buf);
}

/* Global variants -------------------------------------------------------- */

void cbuild_add_global_define(const char *macro) {
    if (macro)
        cbuild__add_define_to_list(&g_global_defines,
                                   &g_global_def_count,
                                   &g_global_def_cap,
                                   macro, NULL);
}

void cbuild_add_global_define_val(const char *macro, const char *val) {
    if (macro)
        cbuild__add_define_to_list(&g_global_defines,
                                   &g_global_def_count,
                                   &g_global_def_cap,
                                   macro, val);
}

void cbuild_set_global_flag(const char *flag, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", !!value);
    cbuild_add_global_define_val(flag, buf);
}

/* Static variables for DFS build function */
static int *visited = NULL;
static int *in_stack = NULL;

/* DFS build function (extended to handle commands as dependencies) */
static void dfs_command_func(command_t *cmd, int *error_flag_ptr) {
    if (!cmd || *error_flag_ptr)
        return;
    if (cmd->executed)
        return;
    for (int i = 0; i < cmd->dep_count; ++i) {
        dfs_command_func(cmd->dependencies[i], error_flag_ptr);
        if (*error_flag_ptr)
            return;
    }
    if (cbuild_run_command(cmd) != 0) {
        *error_flag_ptr = 1;
    }
}

static void dfs_build_func(target_t *t, int *error_flag_ptr) {
    int ti = -1;
    for (int j = 0; j < g_target_count; ++j) {
        if (g_targets[j] == t) {
            ti = j;
            break;
        }
    }
    if (ti == -1)
        return;
    if (*error_flag_ptr)
        return;
    if (in_stack[ti]) {
        fprintf(stderr, "cbuild: Error - circular dependency involving %s\n",
                t->name);
        *error_flag_ptr = 1;
        return;
    }
    if (visited[ti])
        return;
    in_stack[ti] = 1;
    // Run command dependencies first
    for (int ci = 0; ci < t->cmd_count; ++ci) {
        dfs_command_func(t->commands[ci], error_flag_ptr);
        if (*error_flag_ptr) {
            in_stack[ti] = 0;
            return;
        }
    }
    // Then build target dependencies
    for (int di = 0; di < t->dep_count; ++di) {
        dfs_build_func(t->dependencies[di], error_flag_ptr);
        if (*error_flag_ptr) {
            in_stack[ti] = 0;
            return;
        }
    }
    build_target(t, error_flag_ptr);
    // Run post-build commands
    for (int pci = 0; pci < t->post_cmd_count; ++pci) {
        dfs_command_func(t->post_commands[pci], error_flag_ptr);
        if (*error_flag_ptr) {
            in_stack[ti] = 0;
            return;
        }
    }
    visited[ti] = 1;
    in_stack[ti] = 0;
}

// Helper to write a JSON string with proper escaping
static void fprint_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        switch (*s) {
            case '\\':
                fputs("\\\\", f);
                break;
            case '"':
                fputs("\\\"", f);
                break;
            case '\b':
                fputs("\\b", f);
                break;
            case '\f':
                fputs("\\f", f);
                break;
            case '\n':
                fputs("\\n", f);
                break;
            case '\r':
                fputs("\\r", f);
                break;
            case '\t':
                fputs("\\t", f);
                break;
            default:
                if ((unsigned char)*s < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned char)*s);
                } else {
                    fputc(*s, f);
                }
        }
    }
    fputc('"', f);
}

int cbuild_run(int argc, char **argv) {
    cbuild_init();

    // Always clear compile_commands entries at the start of each build
    if (g_cc_entries) {
        for (int i = 0; i < g_cc_count; ++i) {
            free(g_cc_entries[i].directory);
            free(g_cc_entries[i].command);
            free(g_cc_entries[i].file);
        }
        free(g_cc_entries);
        g_cc_entries = NULL;
    }
    g_cc_count = 0;
    g_cc_cap = 0;

    // Pre-collect compile commands for all targets before building
    if (g_generate_compile_commands) {
        for (int i = 0; i < g_target_count; ++i) {
            collect_compile_commands_for_target(g_targets[i]);
        }
    }

    // --- Subproject manifest mode ---
    if (argc > 1 && strcmp(argv[1], "--manifest") == 0) {
        // Print manifest: one line per target: TYPE NAME PATH
        for (int i = 0; i < g_target_count; ++i) {
            target_t *t = g_targets[i];
            // Only print "real" targets (not proxy targets for subprojects)
            if (!t->output_file || !t->name)
                continue;
            // Guess type string
            const char *type = NULL;
            switch (t->type) {
                case TARGET_STATIC_LIB:
                    type = "static_lib";
                    break;
                case TARGET_SHARED_LIB:
                    type = "shared_lib";
                    break;
                case TARGET_EXECUTABLE:
                    type = "executable";
                    break;
                default:
                    continue;
            }
            // Output path relative to cwd (assume output_file is relative)
            printf("%s %s %s\n", type, t->name, t->output_file);
        }
        return 0;
    }
    if (argc > 1) {
        if (strcmp(argv[1], "clean") == 0) {
            cbuild_pretty_step("CLEAN", CBUILD_COLOR_YELLOW,
                               "Cleaning build outputs...");

            // First clean all subprojects
            for (int i = 0; i < g_subproject_count; ++i) {
                subproject_t *sub = g_subprojects[i];
                char *clean_cmd = NULL;

                cbuild_pretty_step("CLEAN", CBUILD_COLOR_YELLOW,
                                   "Cleaning subproject: %s", sub->alias);
#ifdef _WIN32
                append_format(&clean_cmd, "cd /d \"%s\" && \"%s\" clean",
                              sub->directory, sub->cbuild_exe);
#else
                append_format(&clean_cmd, "cd '%s' && '%s' clean", sub->directory,
                              sub->cbuild_exe);
#endif
                int result = run_command(clean_cmd, 0, NULL);
                free(clean_cmd);

                if (result != 0) {
                    fprintf(stderr, "Warning: Failed to clean subproject '%s'\n",
                            sub->alias);
                }
            }

            // Then clean the main project
            for (int i = 0; i < g_target_count; ++i) {
                target_t *t = g_targets[i];
                if (t->obj_dir)
                    remove_dir_recursive(t->obj_dir);
                if (t->output_file)
                    remove_file(t->output_file);
            }

            remove_dir_recursive(g_output_dir);
            cbuild_pretty_status(1, "Clean complete.");
            return 0;
        }
        // Check for custom subcommands
        for (int sci = 0; sci < g_subcommand_count; ++sci) {
            cbuild_subcommand_t *scmd = g_subcommands[sci];
            if (strcmp(argv[1], scmd->name) == 0) {
                // Build the dependency target first
                int error_flag = 0;
                // Allocate and clear visited/in_stack arrays
                if (visited)
                    free(visited);
                if (in_stack)
                    free(in_stack);
                visited = calloc(g_target_count, sizeof(int));
                in_stack = calloc(g_target_count, sizeof(int));
                dfs_build_func(scmd->target, &error_flag);
                free(visited);
                visited = NULL;
                free(in_stack);
                in_stack = NULL;
                if (error_flag) {
                    cbuild_pretty_status(0, "Build failed.");
                    return 1;
                }
                // Run the subcommand
                int rc = 0;
                if (scmd->command_line) {
                    cbuild_pretty_step("SUBCMD", CBUILD_COLOR_BLUE, "Running '%s': %s",
                                       scmd->name, scmd->command_line);
                    rc = run_command(scmd->command_line, 0, NULL);
                } else if (scmd->callback) {
                    cbuild_pretty_step("SUBCMD", CBUILD_COLOR_BLUE,
                                       "Running '%s' (callback)...", scmd->name);
                    scmd->callback(scmd->user_data);
                }
                return rc;
            }
        }
    }
    int error_flag = 0;
    // Allocate and clear visited/in_stack arrays
    if (visited)
        free(visited);
    if (in_stack)
        free(in_stack);
    visited = calloc(g_target_count, sizeof(int));
    in_stack = calloc(g_target_count, sizeof(int));

    for (int i = 0; i < g_target_count; ++i) {
        if (!visited[i]) {
            dfs_build_func(g_targets[i], &error_flag);
            if (error_flag)
                break;
        }
    }
    free(visited);
    visited = NULL;
    free(in_stack);
    in_stack = NULL;
    if (!error_flag) {
        // Dump compile_commands.json if enabled
        if (g_generate_compile_commands) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/compile_commands.json", g_output_dir);
            FILE *f = fopen(path, "w");
            if (f) {
                fprintf(f, "[\n");
                for (int i = 0; i < g_cc_count; i++) {
                    fprintf(f, "  {\"directory\":");
                    fprint_json_string(f, g_cc_entries[i].directory);
                    fprintf(f, ",\"command\":");
                    fprint_json_string(f, g_cc_entries[i].command);
                    fprintf(f, ",\"file\":");
                    fprint_json_string(f, g_cc_entries[i].file);
                    fprintf(f, "}%s\n", (i + 1 < g_cc_count) ? "," : "");
                }
                fprintf(f, "]\n");
                fclose(f);
            }
        }
        cbuild_pretty_status(1, "Build succeeded.");
        return 0;
    } else {
        cbuild_pretty_status(0, "Build failed.");
        return 1;
    }
}

// Initialize global/default build settings
static void cbuild_init() {
    if (!g_output_dir)
        g_output_dir = strdup("build");
    if (!g_cc)
        g_cc = strdup("cc");
    if (!g_ar)
        g_ar = strdup("ar");
#if defined(_WIN32)
    if (!g_ld)
        g_ld = strdup("ld");
#elif defined(__APPLE__) || defined(__linux__)
    if (!g_ld)
        g_ld = strdup(g_cc);  // Use compiler as linker on macOS/Linux
#else
    if (!g_ld)
        g_ld = strdup("ld");
#endif
    if (g_parallel_jobs <= 0) {
        // Try to detect CPU count
        int n = 1;
#ifdef _WIN32
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        n = sysinfo.dwNumberOfProcessors;
#else
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus > 0)
            n = (int)cpus;
#endif
        g_parallel_jobs = n > 0 ? n : 1;
    }
}

// Create a new target struct and add to global list
static target_t *cbuild_create_target(const char *name,
                                      cbuild_target_type type) {
    target_t *t = (target_t *)calloc(1, sizeof(target_t));
    t->type = type;
    t->name = strdup(name);
    // Set output file and obj_dir
    char *out = NULL, *obj = NULL;
    if (type == TARGET_EXECUTABLE) {
#ifdef _WIN32
        append_format(&out, "%s/%s.exe", g_output_dir, name);
#else
        append_format(&out, "%s/%s", g_output_dir, name);
#endif
    } else if (type == TARGET_STATIC_LIB) {
#ifdef _WIN32
        append_format(&out, "%s/%s.lib", g_output_dir, name);
#else
        append_format(&out, "%s/lib%s.a", g_output_dir, name);
#endif
    } else if (type == TARGET_SHARED_LIB) {
#ifdef _WIN32
        append_format(&out, "%s/%s.dll", g_output_dir, name);
#elif __APPLE__
        append_format(&out, "%s/lib%s.dylib", g_output_dir, name);
#else
        append_format(&out, "%s/lib%s.so", g_output_dir, name);
#endif
    }
    append_format(&obj, "%s/obj_%s", g_output_dir, name);
    t->output_file = out;
    t->obj_dir = obj;
    t->commands = NULL;
    t->cmd_count = t->cmd_cap = 0;
    t->post_commands = NULL;
    t->post_cmd_count = t->post_cmd_cap = 0;
    // Add to global list
    ensure_capacity_charpp((char ***)&g_targets, &g_target_count, &g_target_cap);
    g_targets[g_target_count++] = t;
    return t;
}

// Remove a file from disk
static void remove_file(const char *path) {
    if (!path || !*path)
        return;
    remove(path);
}

// Recursively remove a directory and its contents
static void remove_dir_recursive(const char *path) {
    if (!path || !*path)
        return;
#ifdef _WIN32
    WIN32_FIND_DATA ffd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE hFind = FindFirstFile(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", path, ffd.cFileName);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            remove_dir_recursive(full);
        } else {
            remove_file(full);
        }
    } while (FindNextFile(hFind, &ffd));
    FindClose(hFind);
    _rmdir(path);
#else
    DIR *dir = opendir(path);
    if (!dir)
        return;
    struct dirent *entry;
    char buf[1024];
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(buf, sizeof(buf), "%s/%s", path, entry->d_name);
        struct stat st;
        if (!stat(buf, &st)) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(buf);
            } else {
                remove_file(buf);
            }
        }
    }
    closedir(dir);
    rmdir(path);
#endif
}

// Build a target (compile sources, link, etc.)
static void build_target(target_t *t, int *error_flag) {
    // Compile all sources to objects
    int obj_count = t->sources_count;
    char **obj_files = (char **)calloc(obj_count, sizeof(char *));
    for (int i = 0; i < obj_count; ++i) {
        const char *src = t->sources[i];
        const char *slash = strrchr(src, '/');
        const char *base = slash ? slash + 1 : src;
        char *dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s.o", t->obj_dir, (int)len, base);
        obj_files[i] = strdup(objname);

        char depname[512];
        snprintf(depname, sizeof(depname), "%s/%.*s.o.d", t->obj_dir, (int)len,
                 base);

        if (need_recompile(src, objname, depname)) {
            cbuild_pretty_step("COMPILE", CBUILD_COLOR_BLUE, "%s", src);
            if (compile_source(src, objname, depname, t) != 0) {
                *error_flag = 1;
                goto cleanup;
            }
        }
    }

    // Link if needed
    int needs_link = 0;
    struct stat st_out;
    if (stat(t->output_file, &st_out) != 0) {
        needs_link = 1;
    } else {
        // Check if any object file is newer than the output
        for (int i = 0; i < obj_count; ++i) {
            struct stat st_obj;
            if (stat(obj_files[i], &st_obj) != 0 ||
                st_obj.st_mtime > st_out.st_mtime) {
                needs_link = 1;
                break;
            }
        }
        // Check if any dependency output is newer than the output
        if (!needs_link) {
            for (int i = 0; i < t->dep_count; ++i) {
                target_t *dep = t->dependencies[i];
                if (dep->output_file) {
                    struct stat st_dep;
                    if (stat(dep->output_file, &st_dep) == 0) {
                        if (st_dep.st_mtime > st_out.st_mtime) {
                            needs_link = 1;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (needs_link) {
        cbuild_pretty_step("LINK", CBUILD_COLOR_YELLOW, "%s", t->output_file);
        char *cmd = NULL;
        if (t->type == TARGET_STATIC_LIB) {
#ifdef _WIN32
            append_format(&cmd, "%s /OUT:%s", g_ar, t->output_file);
            for (int i = 0; i < obj_count; ++i)
                append_format(&cmd, " %s", obj_files[i]);
#else
            append_format(&cmd, "%s rcs %s", g_ar, t->output_file);
            for (int i = 0; i < obj_count; ++i)
                append_format(&cmd, " %s", obj_files[i]);
#endif
        } else if (t->type == TARGET_EXECUTABLE || t->type == TARGET_SHARED_LIB) {
            append_format(&cmd, "%s -o %s", g_ld, t->output_file);
            for (int i = 0; i < obj_count; ++i)
                append_format(&cmd, " %s", obj_files[i]);
            for (int i = 0; i < t->lib_dir_count; ++i)
#ifdef _WIN32
                append_format(&cmd, " /LIBPATH:\"%s\"", t->lib_dirs[i]);
#else
                append_format(&cmd, " -L\"%s\"", t->lib_dirs[i]);
#endif
            for (int i = 0; i < t->link_lib_count; ++i)
#ifdef _WIN32
                append_format(&cmd, " %s.lib", t->link_libs[i]);
#elif __APPLE__
                append_format(&cmd, " -l%s.dylib", t->link_libs[i]);
#else
                    append_format(&cmd, " -l%s", t->link_libs[i]);
#endif
            for (int i = 0; i < t->dep_count; ++i) {
                target_t *dep = t->dependencies[i];
                if (dep->type == TARGET_STATIC_LIB || dep->type == TARGET_SHARED_LIB)
                    append_format(&cmd, " %s", dep->output_file);
            }
            if (t->ldflags)
                append_format(&cmd, " %s", t->ldflags);
            if (g_global_ldflags)
                append_format(&cmd, " %s", g_global_ldflags);
            if (t->type == TARGET_SHARED_LIB) {
#ifdef _WIN32
                append_format(&cmd, " /DLL");
#else
                append_format(&cmd, " -shared");
#endif
            }
        }
        int rc;
        char *output = NULL;
        rc = run_command(cmd, 1, &output);
        if (output && rc != 0) {
            fwrite(output, 1, strlen(output), stderr);
        }
        if (output)
            free(output);
        free(cmd);
        if (rc != 0) {
            cbuild_pretty_status(0, "Linking failed for %s", t->output_file);
            *error_flag = 1;
            goto cleanup;
        }
    }

cleanup:
    for (int i = 0; i < obj_count; ++i)
        free(obj_files[i]);
    free(obj_files);
}

#endif /* CBUILD_IMPLEMENTATION */