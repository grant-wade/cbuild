/*
------------------------------------------------------------------------------
cbuild.h - Minimal, cross-platform, header-only C build system

Copyright (c) 2025-2026 Grant Wade

1. Purpose
----------
A minimal, cross-platform, header-only build system for C projects.
Write your build logic directly in C, enabling full language power
and portability without external tools or scripts.

2. Key Features
---------------
- Targets: executables, static/shared libraries, file-dependency and dummy targets, custom commands
- Build graph and dependencies with incremental rebuilds
  - Uses timestamps plus compile/link signature files to detect flag/env changes
  - GCC/Clang header deps via .d files; MSVC header deps via /showIncludes parsing
- Parallel compilation across CPU cores (-j/--jobs)
- Sources/includes/lib-dirs and libraries; supports wildcards including ** recursive
- Command API: pre/post target commands, argv-based execution, shell command lines, and C callbacks; command dependencies
- Subcommands: register named actions and run them with --run
- Subprojects: query targets via --manifest, build them on demand, and link as dependencies
- compile_commands.json generation with full arguments array (for clangd, etc.)
- Build configurations: Debug/Release presets, per-target configs, structured knobs (optimization, LTO, sanitizers, etc.)
- Toolchain detection and portability: GCC/Clang/MSVC, platform-specific outputs and SONAME/install_name
- Self-rebuild support to recompile the build executable when its sources change
- Inspection and maintenance: --list, --graph, --deps, --clean
- Embeddable: context-based API with no global state, suitable for library use

3. Basic Usage
--------------
    #define CBUILD_IMPLEMENTATION
    #include "cbuild.h"

    int main(int argc, char** argv) {
        cbuild_context_t* ctx = cbuild_context_new();

        cbuild_set_output_dir(ctx, "build");

        target_t* lib = cbuild_static_library(ctx, "foo");
        cbuild_add_source(ctx, lib, "foo.c");

        target_t* exe = cbuild_executable(ctx, "bar");
        cbuild_add_source(ctx, exe, "bar.c");
        cbuild_add_link_target(ctx, exe, lib);

        int result = cbuild_run(ctx, argc, argv);
        cbuild_context_free(ctx);
        return result;
    }

Typical invocations:
    $ cc build.c -o cbuild            # or 'cl' on Windows
    $ ./cbuild                        # builds all targets
    $ ./cbuild --clean                # cleans build outputs
    $ ./cbuild --version              # prints CBUILD_VERSION
    $ ./cbuild --run bar              # builds bar's target (if any) and runs the subcommand
    $ ./cbuild --list                 # lists targets
    $ ./cbuild --graph                # prints the build graph
    $ ./cbuild --deps=mytarget        # shows reverse dependencies of 'mytarget'
    $ ./cbuild -j 8                   # builds with 8 parallel jobs
    $ ./cbuild --target=mytarget      # builds only the specified target
    $ ./cbuild --compile-commands     # writes compile_commands.json to the output dir

4. Build Configurations
-----------------------
cbuild supports structured build configurations that abstract compiler-specific flags:

    cbuild_context_t* ctx = cbuild_context_new();

    // Use a preset configuration
    config_t* debug = cbuild_config_default_debug(ctx);
    config_t* release = cbuild_config_default_release(ctx);

    // Or create a custom configuration
    config_t* custom = cbuild_config_new(ctx, "Custom");
    cbuild_config_set_opt(ctx, custom, 2);           // -O2 or /O2
    cbuild_config_set_debug(ctx, custom, 1);         // -g or /Zi
    cbuild_config_set_std(ctx, custom, "c11");       // -std=c11 or /std:c11
    cbuild_config_set_warnings(ctx, custom, 2);      // -Wall -Wextra -Wpedantic or /W4
    cbuild_config_set_lto(ctx, custom, 1);           // -flto or /GL + /LTCG
    cbuild_config_enable_sanitizers(ctx, custom, 1); // -fsanitize=address
    cbuild_config_set_output_dir(ctx, custom, "build/custom");

    // Set active configuration (applies to all targets by default)
    cbuild_set_active_config(ctx, release);

    // Or override per-target
    cbuild_target_set_config(ctx, my_debug_tool, debug);

5. Programmatic / Library Use
-----------------------------
For embedding cbuild or using it programmatically without CLI parsing:

    cbuild_context_t* ctx = cbuild_context_new();

    // Configure the build
    cbuild_set_output_dir(ctx, "build");
    cbuild_set_compiler(ctx, "clang");
    cbuild_set_parallelism(ctx, 4);

    // Define targets...
    target_t* exe = cbuild_executable(ctx, "myapp");
    cbuild_add_source(ctx, exe, "main.c");

    // Build programmatically (without CLI)
    int result = cbuild_build(ctx, NULL);        // build all targets
    int result = cbuild_build(ctx, "myapp");     // build specific target

    // Or configure from argv without running
    cbuild_configure_from_argv(ctx, argc, argv);

    // Check errors
    const char* err = cbuild_get_last_error(ctx);
    if (err) fprintf(stderr, "Error: %s\n", err);

    // Reset for reuse (clears targets, keeps allocator)
    cbuild_reset(ctx);

    // Clean up
    cbuild_context_free(ctx);

6. Custom Logging
-----------------
Replace the default logger for integration with other systems:

    void my_logger(void* user_data, cbuild_log_level_t level, const char* msg) {
        MyApp* app = (MyApp*)user_data;
        // Route to your logging system
    }

    cbuild_set_logger(ctx, my_logger, my_app);

7. Integration Notes
--------------------
- Single header: include in your build.c and define CBUILD_IMPLEMENTATION in exactly one translation unit
- Context-based: all state lives in cbuild_context_t, no globals, safe for library embedding
- Subprojects: add with cbuild_add_subproject() and link via cbuild_subproject_get_target()
- Argv-based process execution avoids shell quoting issues on all platforms
- Wildcards supported for sources/includes/libs (including ** recursive)
- Platform headers abstract Windows, macOS, and Linux differences
- Self-rebuild: call cbuild_self_rebuild_if_needed() or use CBUILD_SELF_REBUILD macro

8. License
----------
BSD 3-Clause License

Copyright (c) 2025-2026 Grant Wade
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

/* Public semantic version. Keep this in sync with release tags. */
#define CBUILD_VERSION "v0.1.1"
#define CBUILD_VERSION_MAJOR 0
#define CBUILD_VERSION_MINOR 1
#define CBUILD_VERSION_PATCH 1

#ifdef __cplusplus
extern "C" {
#endif

// *** Type Definitions *** //
typedef struct cbuild_context cbuild_context_t;
typedef struct cbuild_target target_t;
typedef struct cbuild_command command_t;
typedef struct cbuild_subproject subproject_t;
typedef struct cbuild_config config_t;

typedef void (*cbuild_subcommand_callback)(void* user_data);
typedef int (*cbuild_flag_callback)(const char* value, void* user_data);

typedef enum {
    CBUILD_FLAG_PRE = 0,          /* parse-time actions; set globals, early exits */
    CBUILD_FLAG_BEFORE_BUILD = 1, /* runs after graph is ready, before build */
    CBUILD_FLAG_AFTER_BUILD = 2   /* runs after a successful build */
} cbuild_flag_phase_t;

/* Special return code to signal "handled & exit(0)". */
#define CBUILD_FLAG_EXIT 777

// *** Logging System *** //
typedef enum {
    CBUILD_LOG_INFO = 0,
    CBUILD_LOG_WARNING,
    CBUILD_LOG_ERROR,
    CBUILD_LOG_STEP,
    CBUILD_LOG_STATUS_OK,
    CBUILD_LOG_STATUS_FAIL,
    CBUILD_LOG_VERBOSE
} cbuild_log_level_t;

typedef void (*cbuild_log_fn)(void* user_data, cbuild_log_level_t level, const char* msg);

// *** Context API *** //
cbuild_context_t* cbuild_context_new(void);
void cbuild_context_free(cbuild_context_t* ctx);
void cbuild_set_logger(cbuild_context_t* ctx, cbuild_log_fn callback, void* user_data);
const char* cbuild_get_last_error(cbuild_context_t* ctx);
void cbuild_set_user_data(cbuild_context_t* ctx, void* data);
void* cbuild_get_user_data(cbuild_context_t* ctx);

// *** Main Entrypoint *** //
int cbuild_run(cbuild_context_t* ctx, int argc, char** argv);

// *** Programmatic Build API *** //
int cbuild_build(cbuild_context_t* ctx, const char* target_name);
int cbuild_clean(cbuild_context_t* ctx);
int cbuild_configure_from_argv(cbuild_context_t* ctx, int argc, char** argv);

// *** Reset Build State *** //
void cbuild_reset(cbuild_context_t* ctx);
void cbuild_teardown(cbuild_context_t* ctx);

// *** Target creation functions *** //
target_t* cbuild_executable(cbuild_context_t* ctx, const char* name);
target_t* cbuild_static_library(cbuild_context_t* ctx, const char* name);
target_t* cbuild_shared_library(cbuild_context_t* ctx, const char* name);
target_t* cbuild_dummy_target(cbuild_context_t* ctx, const char* name);
target_t* cbuild_file_dep_target(cbuild_context_t* ctx, const char* name, const char* file_path);

// *** Target sources/includes/links/flags setup *** //
// All functions return 0 on success, -1 on failure (sets last_error)
int cbuild_add_source(cbuild_context_t* ctx, target_t* target, const char* source_file);
int cbuild_add_include_dir(cbuild_context_t* ctx, target_t* target, const char* include_path);
int cbuild_add_library_dir(cbuild_context_t* ctx, target_t* target, const char* lib_dir);
int cbuild_add_link_library(cbuild_context_t* ctx, target_t* target, const char* lib_name);
int cbuild_add_link_target(cbuild_context_t* ctx, target_t* dependant, target_t* dependency);
int cbuild_add_expose_library(cbuild_context_t* ctx, target_t* target, const char* lib_path);
int cbuild_export_symbols(cbuild_context_t* ctx, target_t* target);
int cbuild_add_cflags(cbuild_context_t* ctx, target_t* target, const char* cflags);
int cbuild_add_ldflags(cbuild_context_t* ctx, target_t* target, const char* ldflags);
int cbuild_add_flag(cbuild_context_t* ctx, target_t* target, const char* flag, int value);
int cbuild_add_define(cbuild_context_t* ctx, target_t* target, const char* macro);
int cbuild_add_define_val(cbuild_context_t* ctx, target_t* target, const char* macro, const char* value);

// *** Context Settings *** //
void cbuild_set_output_dir(cbuild_context_t* ctx, const char* dir);
void cbuild_set_parallelism(cbuild_context_t* ctx, int jobs_count);
void cbuild_set_compiler(cbuild_context_t* ctx, const char* compiler_exe);
void cbuild_set_linker(cbuild_context_t* ctx, const char* linker_exe);
void cbuild_set_archiver(cbuild_context_t* ctx, const char* archiver_exe);
void cbuild_target_set_linker(cbuild_context_t* ctx, target_t* t, const char* linker_exe);
void cbuild_set_output_file(cbuild_context_t* ctx, target_t* t, const char* path);
void cbuild_set_soname(cbuild_context_t* ctx, target_t* t, const char* soname);
void cbuild_set_build_type(cbuild_context_t* ctx, const char* build_type);
void cbuild_add_global_cflags(cbuild_context_t* ctx, const char* flags);
void cbuild_add_global_ldflags(cbuild_context_t* ctx, const char* flags);
void cbuild_add_global_define(cbuild_context_t* ctx, const char* macro);
void cbuild_add_global_define_val(cbuild_context_t* ctx, const char* macro, const char* value);
void cbuild_add_global_flag(cbuild_context_t* ctx, const char* flag, int value);
void cbuild_enable_compile_commands(cbuild_context_t* ctx, int enabled);
void cbuild_guess_compiler(cbuild_context_t* ctx);
int cbuild_self_rebuild_if_needed(cbuild_context_t* ctx, int argc, char** argv, const char** sources, int sources_count);
void cbuild_set_verbose(cbuild_context_t* ctx, int verbose);

// *** Build Configuration API *** //
config_t* cbuild_config_new(cbuild_context_t* ctx, const char* name);
config_t* cbuild_config_default_debug(cbuild_context_t* ctx);
config_t* cbuild_config_default_release(cbuild_context_t* ctx);
config_t* cbuild_config_default_wasm32(cbuild_context_t* ctx);

void cbuild_set_active_config(cbuild_context_t* ctx, config_t*);
void cbuild_target_set_config(cbuild_context_t* ctx, target_t*, config_t*);

void cbuild_config_free(cbuild_context_t* ctx, config_t*);
void cbuild_config_add_cflags(cbuild_context_t* ctx, config_t*, const char* flags);
void cbuild_config_add_ldflags(cbuild_context_t* ctx, config_t*, const char* flags);
void cbuild_config_add_define(cbuild_context_t* ctx, config_t*, const char* def);
void cbuild_config_add_include(cbuild_context_t* ctx, config_t*, const char* dir);
void cbuild_config_add_libdir(cbuild_context_t* ctx, config_t*, const char* dir);
void cbuild_config_add_linklib(cbuild_context_t* ctx, config_t*, const char* lib);
void cbuild_config_set_opt(cbuild_context_t* ctx, config_t*, int level);
void cbuild_config_set_debug(cbuild_context_t* ctx, config_t*, int on);
void cbuild_config_set_lto(cbuild_context_t* ctx, config_t*, int mode);
void cbuild_config_set_pic(cbuild_context_t* ctx, config_t*, int on);
void cbuild_config_set_warnings(cbuild_context_t* ctx, config_t*, int mode);
void cbuild_config_set_std(cbuild_context_t* ctx, config_t*, const char* std);
void cbuild_config_set_runtime(cbuild_context_t* ctx, config_t*, const char* runtime);
void cbuild_config_set_output_dir(cbuild_context_t* ctx, config_t*, const char* output_dir);
void cbuild_config_set_freestanding(cbuild_context_t* ctx, config_t*, int on);
void cbuild_config_enable_sanitizers(cbuild_context_t* ctx, config_t*, int mask);
void cbuild_config_disable_sanitizers(cbuild_context_t* ctx, config_t*, int mask);
void cbuild_config_set_compiler(cbuild_context_t* ctx, config_t*, const char* compiler);
void cbuild_config_set_linker(cbuild_context_t* ctx, config_t*, const char* linker);

// *** Command API *** //
int cbuild_run_command(cbuild_context_t* ctx, command_t* cmd);
command_t* cbuild_command(cbuild_context_t* ctx, const char* name, const char* command_line);
command_t* cbuild_command_argv(cbuild_context_t* ctx, const char* name, char** argv, int argc);
command_t* cbuild_command_function(cbuild_context_t* ctx, const char* name,
                                   cbuild_subcommand_callback callback,
                                   void* user_data);
void cbuild_target_add_command(cbuild_context_t* ctx, target_t* target, command_t* cmd);
void cbuild_target_add_post_command(cbuild_context_t* ctx, target_t* target, command_t* cmd);
void cbuild_command_add_dependency(cbuild_context_t* ctx, command_t* cmd, command_t* dependency);

// *** Subproject API *** //
subproject_t* cbuild_add_subproject(cbuild_context_t* ctx, const char* alias, const char* directory,
                                    const char* cbuild_exe);
target_t* cbuild_subproject_get_target(cbuild_context_t* ctx, subproject_t* sub, const char* tgt_name);
void cbuild_register_subcommand(cbuild_context_t* ctx, const char* name, target_t* target,
                                const char* command_line,
                                cbuild_subcommand_callback callback,
                                void* user_data);

/* Register a flag handler. */
void cbuild_register_flag(cbuild_context_t* ctx, const char* long_name,
                          char short_name,
                          int takes_value,
                          cbuild_flag_phase_t phase,
                          const char* help,
                          cbuild_flag_callback cb,
                          void* user_data);

/* Convenience registrations that bind outputs directly. */
void cbuild_register_flag_bool(cbuild_context_t* ctx, const char* long_name, char short_name,
                               cbuild_flag_phase_t phase, const char* help,
                               int* out_bool);

void cbuild_register_flag_int(cbuild_context_t* ctx, const char* long_name, char short_name,
                              cbuild_flag_phase_t phase, const char* help,
                              int* out_int);

void cbuild_register_flag_str(cbuild_context_t* ctx, const char* long_name, char short_name,
                              cbuild_flag_phase_t phase, const char* help,
                              const char** out_str);

// *** Argument Parsing Helpers *** //
int cbuild_has_flag(int argc, char** argv, const char* long_name, char short_name);

// *** File System and Other Helpers *** //
int cbuild_file_exists(const char* path);
int cbuild_dir_exists(const char* path);
int cbuild_remove_file(const char* path);
int cbuild_remove_dir(const char* path);
int cbuild_get_cwd(char* buf, long size);
int cbuild_match_wildcard(const char* pattern, const char* string);
int cbuild_expand_wildcard(const char* pattern, char*** files, int* file_count);
int cbuild_expand_wildcard_recursive(const char* dir_path, const char* pattern,
                                     char*** files, int* file_count,
                                     int* capacity);

// *** Convenience Macros (require ctx variable in scope) *** //
#define CBUILD_SUBPROJECT(CTX, ALIAS, DIR, EXE) \
    subproject_t* ALIAS = cbuild_add_subproject((CTX), #ALIAS, DIR, EXE)

#define CBUILD_SELF_REBUILD(CTX, ARGC, ARGV, ...)                             \
    do {                                                                      \
        const char* _srcs[] = { __VA_ARGS__ };                                \
        if (cbuild_self_rebuild_if_needed((CTX), (ARGC), (ARGV), _srcs,       \
                                      (int)(sizeof(_srcs) / sizeof(*_srcs))) != 0) \
            return 1;                                                         \
    } while (0)

#define CBUILD_SOURCES(CTX, TGT, ...)                                \
    do {                                                             \
        const char* _a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_source((CTX), (TGT), _a[_i]);                 \
    } while (0)

#define CBUILD_INCLUDES(CTX, TGT, ...)                               \
    do {                                                             \
        const char* _a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_include_dir((CTX), (TGT), _a[_i]);            \
    } while (0)

#define CBUILD_LIB_DIRS(CTX, TGT, ...)                               \
    do {                                                             \
        const char* _a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_library_dir((CTX), (TGT), _a[_i]);            \
    } while (0)

#define CBUILD_LINK_LIBS(CTX, TGT, ...)                              \
    do {                                                             \
        const char* _a[] = { __VA_ARGS__ };                          \
        for (int _i = 0; _i < (int)(sizeof(_a) / sizeof(*_a)); _i++) \
            cbuild_add_link_library((CTX), (TGT), _a[_i]);           \
    } while (0)

#define CBUILD_EXECUTABLE(CTX, NAME, ...)     \
    do {                                      \
        NAME = cbuild_executable((CTX), #NAME); \
        __VA_ARGS__                           \
    } while (0)

#define CBUILD_STATIC_LIBRARY(CTX, NAME, ...)     \
    do {                                          \
        NAME = cbuild_static_library((CTX), #NAME); \
        __VA_ARGS__                               \
    } while (0)

#define CBUILD_SHARED_LIBRARY(CTX, NAME, ...)     \
    do {                                          \
        NAME = cbuild_shared_library((CTX), #NAME); \
        __VA_ARGS__                               \
    } while (0)

#define CBUILD_DEFINES(CTX, TGT, ...)                                      \
    do {                                                                   \
        const char* _defs[] = { __VA_ARGS__ };                             \
        for (int _i = 0; _i < (int)(sizeof(_defs) / sizeof(*_defs)); _i++) \
            cbuild_add_define((CTX), (TGT), _defs[_i]);                    \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* CBUILD_H */
