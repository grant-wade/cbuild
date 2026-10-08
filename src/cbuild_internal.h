/* cbuild_internal.h - platform setup, private types, and helpers shared by the
   cbuild sources. Not part of the public API. */
#ifndef CBUILD_INTERNAL_H
#define CBUILD_INTERNAL_H

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <ctype.h>
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
#define chdir _chdir
#define PATH_MAX MAX_PATH
/* POSIX compatibility macros for Windows */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#define strtok_r strtok_s
#define strcasecmp _stricmp
#define strdup _strdup
#define isatty _isatty
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
/* Avoid <sys/sysctl.h>: strict POSIX feature modes hide BSD u_int types
 * required by that header in recent macOS SDKs. */
extern int sysctlbyname(const char* name, void* oldp, size_t* oldlenp,
                        void* newp, size_t newlen);
#endif
#include <pthread.h>
#include <strings.h>
#include <unistd.h>

extern char** environ; /* for posix_spawnp */
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096  // reasonable default for most systems
#endif

#define CBUILD_EXE_SUFFIX ".exe"

#ifndef _WIN32
#define CBUILD_COLOR_RESET "\033[0m"
#define CBUILD_COLOR_BOLD "\033[1m"
#define CBUILD_COLOR_GREEN "\033[32m"
#define CBUILD_COLOR_YELLOW "\033[33m"
#define CBUILD_COLOR_BLUE "\033[34m"
#define CBUILD_COLOR_MAGENTA "\033[35m"
#define CBUILD_COLOR_RED "\033[31m"
#define CBUILD_OBJ_EXT ".o"
#else
#define CBUILD_COLOR_RESET ""
#define CBUILD_COLOR_BOLD ""
#define CBUILD_COLOR_GREEN ""
#define CBUILD_COLOR_YELLOW ""
#define CBUILD_COLOR_BLUE ""
#define CBUILD_COLOR_MAGENTA ""
#define CBUILD_COLOR_RED ""
#define CBUILD_OBJ_EXT ".obj"
#endif

#include "cbuild.h"

/* Helpers shared between source files have external linkage when src/ is
   built as separate translation units, and stay private to the including
   translation unit in the amalgamated single-header build. */
#ifdef CBUILD_IMPLEMENTATION
#define CBUILD_INTERNAL static
#else
#define CBUILD_INTERNAL
#endif

/* Modification time in the finest unit the platform reports (100ns ticks on
 * Windows, nanoseconds elsewhere). Only meaningful for comparing two files. */
typedef unsigned long long cbuild_mtime_t;

/* --- Data Structures for Build Targets, Commands, and Build Config --- */

typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_SHARED_LIB,
    TARGET_COMMAND,
    TARGET_DUMMY,
    TARGET_FILE_DEP
} cbuild_target_type;

typedef enum {
    CBUILD_CC_GCC_CLANG,
    CBUILD_CC_MSVC
} cbuild_cc_kind_t;

/* Global list of subcommands */
typedef struct cbuild_subcommand {
    char* name;
    target_t* target;
    char* command_line;
    cbuild_subcommand_callback callback;
    void* user_data;
} cbuild_subcommand_t;

typedef struct {
    char* directory;
    char* command;
    char** arguments;
    int argc;
    char* file;
} compile_commands_entry_t;

typedef struct cbuild_flag_handler {
    char* long_name;
    char short_name;
    int takes_value;
    cbuild_flag_phase_t phase;
    char* help;
    cbuild_flag_callback cb;
    void* user_data;
    /* direct bindings for convenience variants */
    int* bind_bool;
    int* bind_int;
    const char** bind_str;
    char* allocated_str;  /* owned copy of string value, freed on reset */
} cbuild_flag_handler_t;

typedef struct {
    target_t* target;
    int source_index;
} compile_job_t;

typedef struct cbuild_subproject_target {
    char* name;                          // logical name (e.g. "zlib")
    char* type;                          // "static_lib", "shared_lib", "executable"
    char* output_path;                   // relative to subproject directory
    struct cbuild_target* proxy_target;  // created on demand
} cbuild_subproject_target_t;

struct cbuild_subproject {
    char* alias;
    char* directory;
    char* cbuild_exe;
    command_t* build_cmd;
    int manifest_loaded;
    cbuild_subproject_target_t* targets;
    int target_count, target_cap;
};

/* The main context structure that holds all build state */
struct cbuild_context {
    /* Compiler kind */
    cbuild_cc_kind_t cc_kind;

    /* Logging callback and user data */
    cbuild_log_fn log_fn;
    void* log_user_data;

    /* Target list */
    target_t** targets;
    int target_count;
    int target_cap;

    /* Command list */
    command_t** commands;
    int command_count;
    int command_cap;

    /* Build settings */
    char* output_dir;
    int parallel_jobs;
    char* cc;
    char* ar;
    char* ld;
    char** global_cflags;
    int global_cflag_count;
    int global_cflag_cap;
    char** global_ldflags;
    int global_ldflag_count;
    int global_ldflag_cap;
    int verbose;
    char** target_filters;
    int target_filter_count;
    int target_filter_cap;

    /* Build configurations */
    config_t* default_config;
    config_t* active_config;
    target_t** cfg_targets;
    config_t** cfg_values;
    int cfg_count;
    int cfg_cap;

    /* All configs created via cbuild_config_new (for automatic cleanup) */
    config_t** all_configs;
    int all_configs_count;
    int all_configs_cap;

    /* DFS state */
    int* visited;
    int* in_stack;

    /* Global defines */
    char** global_defines;
    int global_def_count;
    int global_def_cap;

    /* Subcommands */
    cbuild_subcommand_t** subcommands;
    int subcommand_count;
    int subcommand_cap;

    /* Compile commands generation */
    int generate_compile_commands;
    compile_commands_entry_t* cc_entries;
    int cc_count;
    int cc_cap;

    /* Flag handlers */
    cbuild_flag_handler_t* flag_handlers;
    int flag_count;
    int flag_cap;

    /* Help display */
    const char* argv0_for_help;

    /* Job queue for parallel compilation */
    compile_job_t* job_queue;
    int job_count;
    int job_capacity;
    int jobs_completed;
    int compiled_count;

    /* Threading primitives */
#ifdef _WIN32
    HANDLE* threads;
    CRITICAL_SECTION queue_mutex;
#else
    pthread_t* threads;
    pthread_mutex_t queue_mutex;
#endif

    /* Build error flag */
    int build_error;

    /* Last error message for error propagation */
    char last_error[512];

    /* Subprojects */
    subproject_t** subprojects;
    int subproject_count;
    int subproject_cap;

    /* Run subcommand name */
    const char* run_subcmd;

    /* User data for embedders */
    void* user_data;
};

struct cbuild_command {
    char* name;
    char* command_line;
    char** argv;
    int argc;
    cbuild_subcommand_callback callback;
    void* user_data;
    command_t** dependencies;
    int dep_count, dep_cap;
    int executed;
    int in_progress;  // set while running, to detect dependency cycles
    int result;
};

struct cbuild_config {
    const char* name;

    char** cflags;
    int ncflags;
    int cflags_cap;
    char** ldflags;
    int nldflags;
    int ldflags_cap;
    char** defines;
    int ndefines;
    int defines_cap;
    char** includes;
    int nincludes;
    int includes_cap;
    char** libdirs;
    int nlibdirs;
    int libdirs_cap;
    char** linklibs;
    int nlinklibs;
    int linklibs_cap;

    // structured knobs (portable intent → flags later)
    int opt_level;        // 0..3, or -1 = leave alone
    int debug_symbols;    // bool
    int lto;              // 0=off, 1=on, 2=thin
    int pic;              // bool
    int warnings;         // 0=default, 1=all, 2=pedantic
    int freestanding;     // bool
    const char* output_dir;  // custom build dir for this config
    const char* std;      // "c11","c17","gnu11", etc.
    const char* runtime;  // "static","dynamic" (where it makes sense)
    int sanitize;         // bitmask: ASAN|UBSAN|TSAN|… (0 none)

    // per-config tool overrides
    const char* compiler; // Override global g_cc
    const char* linker;   // Override global g_ld
};

/* Structure representing a build target (executable or library) */
struct cbuild_target {
    cbuild_target_type type;
    char* name;      // base name of target
    char** sources;  // array of source file paths
    int sources_count;
    int sources_cap;
    char** include_dirs;  // array of include directory paths
    int include_count;
    int include_cap;
    char** lib_dirs;  // library directories for linking
    int lib_dir_count;
    int lib_dir_cap;
    char** link_libs;  // external libraries to link (names or paths)
    int link_lib_count;
    int link_lib_cap;
    char** exposed_libs;
    int exposed_lib_count;
    int exposed_lib_cap;
    target_t** dependencies;  // other targets this target depends on (to link against)
    int dep_count;
    int dep_cap;
    char** cflags;  // compile flags tokens specific to this target
    int cflag_count;
    int cflag_cap;
    char** ldflags;  // linker flags tokens specific to this target
    int ldflag_count;
    int ldflag_cap;
    char* output_file;     // path to final output (exe, .a, .dll/.so)
    int output_file_explicit;  // set by cbuild_set_output_file; never re-derived
    char* obj_dir;         // directory for this target's object files (and .d files)
    command_t** commands;  // commands to run before building this target
    int cmd_count, cmd_cap;
    command_t** post_commands;  // commands to run after building this target
    int post_cmd_count, post_cmd_cap;
    char** defines;
    int define_count;
    int define_cap;
    char* soname;  // SONAME (Linux) or install_name (macOS) for shared libraries
    int config_applied;
    int external;  // output is produced by a subproject, not linked by this context
    int rebuilt;   // output was (re)produced during the current build pass

    // per-target tool overrides (from config or explicit)
    char* compiler;        // Override global g_cc for this target
    char* linker;          // Override global g_ld for this target
    int cc_kind_override;  // -1 = use global g_cc_kind, else cbuild_cc_kind_t
};

#define CBUILD_MAX(a, b) ((a) > (b) ? (a) : (b))

/* --- Process Spawning API (for subproject builds) --- */
typedef struct {
    char** args;
    int count;
    int capacity;
} cbuild_argv_t;

/* build.c */
CBUILD_INTERNAL void dfs_build_func(cbuild_context_t* ctx, target_t* t, int* error_flag_ptr);
CBUILD_INTERNAL void cbuild__resolve_target_paths(cbuild_context_t* ctx);

/* compile.c */
CBUILD_INTERNAL int process_compile_jobs_parallel(cbuild_context_t* ctx, target_t* target, int* error_flag);
CBUILD_INTERNAL void collect_compile_commands_for_target(cbuild_context_t* ctx, target_t* t);

/* config.c */
CBUILD_INTERNAL void cbuild__apply_config_if_needed(cbuild_context_t* ctx, target_t* t);

/* context.c */
CBUILD_INTERNAL void cbuild_init(cbuild_context_t* ctx);

/* flags.c */
CBUILD_INTERNAL void cbuild__print_help(cbuild_context_t* ctx);
CBUILD_INTERNAL int cbuild_dispatch_flags_strict(cbuild_context_t* ctx, cbuild_flag_phase_t phase,
                                                 int* argc, char*** argvp);

/* fs.c */
CBUILD_INTERNAL char* cbuild__normalize_path(const char* path);
CBUILD_INTERNAL int cbuild__mtime(const char* path, cbuild_mtime_t* out);
CBUILD_INTERNAL char* cbuild__join_path(const char* a, const char* b);
CBUILD_INTERNAL int cbuild__is_library_path(const char* path);
CBUILD_INTERNAL void get_dir_from_path(const char* path, char* out_dir, size_t out_size);
CBUILD_INTERNAL int ensure_dir_exists(const char* path);
CBUILD_INTERNAL int cbuild__strcmp_wrapper(const void* a, const void* b);
CBUILD_INTERNAL void remove_file(cbuild_context_t* ctx, const char* path);
CBUILD_INTERNAL void cbuild__clean_dir(cbuild_context_t* ctx, const char* path);

/* log.c */
CBUILD_INTERNAL void cbuild__log(cbuild_context_t* ctx, cbuild_log_level_t level, const char* fmt, ...);
CBUILD_INTERNAL void cbuild__log_step(cbuild_context_t* ctx, const char* label, const char* color, const char* fmt, ...);
CBUILD_INTERNAL void cbuild__log_status(cbuild_context_t* ctx, int ok, const char* fmt, ...);
CBUILD_INTERNAL void cbuild__set_error(cbuild_context_t* ctx, const char* fmt, ...);

/* process.c */
CBUILD_INTERNAL void cbuild_argv_init(cbuild_argv_t* argv);
CBUILD_INTERNAL void cbuild_argv_free(cbuild_argv_t* argv);
CBUILD_INTERNAL int cbuild_argv_append(cbuild_argv_t* argv, const char* arg);
CBUILD_INTERNAL char* cbuild_argv_to_cmdline(cbuild_argv_t* argv);
CBUILD_INTERNAL int cbuild_spawn_process(cbuild_context_t* ctx, cbuild_argv_t* argv, int capture_output, char** captured_output);
CBUILD_INTERNAL int run_command(cbuild_context_t* ctx, const char* cmd, int capture_out,
                                char** captured_output);
CBUILD_INTERNAL void cbuild__argv_append_prefixed(cbuild_argv_t* argv, const char* prefix, const char* value);
CBUILD_INTERNAL void cbuild_argv_append_flags(cbuild_argv_t* argv, const char* flags);

/* target.c */
CBUILD_INTERNAL int cbuild__add_define_to_list(cbuild_context_t* ctx, char*** arr, int* count, int* cap,
                                                const char* macro,
                                                const char* value_optional);

/* toolchain.c */
CBUILD_INTERNAL cbuild_cc_kind_t detect_cc_kind(const char* exe);
CBUILD_INTERNAL cbuild_cc_kind_t detect_ar_kind(const char* ar);
CBUILD_INTERNAL const char* cbuild_target_compiler(cbuild_context_t* ctx, target_t* t);
CBUILD_INTERNAL cbuild_cc_kind_t cbuild_target_cc_kind(cbuild_context_t* ctx, target_t* t);
CBUILD_INTERNAL const char* cbuild_target_linker(cbuild_context_t* ctx, target_t* t);

/* util.c */
CBUILD_INTERNAL void cbuild__trim(char* s);
CBUILD_INTERNAL int ensure_capacity_charpp(cbuild_context_t* ctx, char*** arr, int* count, int* capacity);
CBUILD_INTERNAL int append_format(char** dst, const char* fmt, ...);
CBUILD_INTERNAL int cbuild__has_suffix(const char* s, const char* suf);
CBUILD_INTERNAL unsigned cbuild__hash_path(const char* s);
CBUILD_INTERNAL void fprint_json_string(FILE* f, const char* s);

#endif /* CBUILD_INTERNAL_H */
