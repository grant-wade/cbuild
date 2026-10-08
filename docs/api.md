# CBuild API reference

This is a compact reference for the public API in `cbuild.h`. Include the header normally wherever declarations are needed and define `CBUILD_IMPLEMENTATION` in exactly one C translation unit.

All build state is owned by a `cbuild_context_t`. Unless noted otherwise, functions that create targets, commands, subprojects, and configurations attach them to the context and their storage is released by `cbuild_context_free`.

## Version

```c
#define CBUILD_VERSION "v0.1.1"
#define CBUILD_VERSION_MAJOR 0
#define CBUILD_VERSION_MINOR 1
#define CBUILD_VERSION_PATCH 1
```

Release tags exactly match `CBUILD_VERSION`. The numeric macros are suitable for preprocessor checks.

## Types

| Type | Purpose |
| --- | --- |
| `cbuild_context_t` | Build state and settings |
| `target_t` | Build graph target |
| `command_t` | Shell, argv, or callback command |
| `subproject_t` | Registered CBuild subproject |
| `config_t` | Reusable build configuration |
| `cbuild_log_level_t` | Logger message classification |
| `cbuild_flag_phase_t` | Custom flag execution phase |

## Context lifecycle and logging

```c
cbuild_context_t *cbuild_context_new(void);
void cbuild_context_free(cbuild_context_t *ctx);
void cbuild_reset(cbuild_context_t *ctx);
void cbuild_teardown(cbuild_context_t *ctx);

void cbuild_set_logger(cbuild_context_t *ctx, cbuild_log_fn callback,
                       void *user_data);
const char *cbuild_get_last_error(cbuild_context_t *ctx);
void cbuild_set_user_data(cbuild_context_t *ctx, void *data);
void *cbuild_get_user_data(cbuild_context_t *ctx);
```

`cbuild_reset` releases graph state and reinitializes the context for reuse. `cbuild_teardown` releases internal state without freeing the context itself. Most applications should simply call `cbuild_context_free`.

## Running, building, and cleaning

```c
int cbuild_run(cbuild_context_t *ctx, int argc, char **argv);
int cbuild_build(cbuild_context_t *ctx, const char *target_name);
int cbuild_clean(cbuild_context_t *ctx);
int cbuild_configure_from_argv(cbuild_context_t *ctx, int argc, char **argv);
```

- `cbuild_run` processes the full CLI and performs the requested operation.
- `cbuild_build` bypasses CLI parsing. Pass `NULL` to build all targets.
- `cbuild_clean` removes outputs represented by the context and cleans subprojects. It deletes each target's object directory and output file, then the output directory itself. Symbolic links are removed rather than followed, a symlinked output directory only loses the outputs CBuild knows about, and an output directory that is or contains the working directory is left alone. File-dependency targets with no commands are treated as inputs and are never deleted.
- `cbuild_configure_from_argv` processes the common configuration options without starting a build.

## Creating targets

```c
target_t *cbuild_executable(cbuild_context_t *ctx, const char *name);
target_t *cbuild_static_library(cbuild_context_t *ctx, const char *name);
target_t *cbuild_shared_library(cbuild_context_t *ctx, const char *name);
target_t *cbuild_dummy_target(cbuild_context_t *ctx, const char *name);
target_t *cbuild_file_dep_target(cbuild_context_t *ctx, const char *name,
                                 const char *file_path);
```

A dummy target represents graph ordering without an output. A file-dependency target represents a generated or externally supplied file and can have sources and commands attached to it. Its commands run when the file is missing, older than one of its sources, or older than the output of a target it depends on; those dependencies (for example a generator executable) are built first.

## Populating targets

The following mutators return `0` on success and `-1` on failure, with details available from `cbuild_get_last_error`:

```c
int cbuild_add_source(cbuild_context_t *ctx, target_t *target,
                      const char *source_file);
int cbuild_add_include_dir(cbuild_context_t *ctx, target_t *target,
                           const char *include_path);
int cbuild_add_library_dir(cbuild_context_t *ctx, target_t *target,
                           const char *lib_dir);
int cbuild_add_link_library(cbuild_context_t *ctx, target_t *target,
                            const char *lib_name);
int cbuild_add_link_target(cbuild_context_t *ctx, target_t *dependant,
                           target_t *dependency);
int cbuild_add_expose_library(cbuild_context_t *ctx, target_t *target,
                              const char *lib_path);
int cbuild_export_symbols(cbuild_context_t *ctx, target_t *target);
int cbuild_add_cflags(cbuild_context_t *ctx, target_t *target,
                      const char *cflags);
int cbuild_add_ldflags(cbuild_context_t *ctx, target_t *target,
                       const char *ldflags);
int cbuild_add_flag(cbuild_context_t *ctx, target_t *target,
                    const char *flag, int value);
int cbuild_add_define(cbuild_context_t *ctx, target_t *target,
                      const char *macro);
int cbuild_add_define_val(cbuild_context_t *ctx, target_t *target,
                          const char *macro, const char *value);
```

Source, include, and library paths may contain wildcard patterns. `**` performs recursive matching.

## Context and toolchain settings

```c
void cbuild_set_output_dir(cbuild_context_t *ctx, const char *dir);
void cbuild_set_parallelism(cbuild_context_t *ctx, int jobs_count);
void cbuild_set_compiler(cbuild_context_t *ctx, const char *compiler_exe);
void cbuild_set_linker(cbuild_context_t *ctx, const char *linker_exe);
void cbuild_set_archiver(cbuild_context_t *ctx, const char *archiver_exe);
void cbuild_target_set_linker(cbuild_context_t *ctx, target_t *target,
                              const char *linker_exe);
void cbuild_set_output_file(cbuild_context_t *ctx, target_t *target,
                            const char *path);
void cbuild_set_soname(cbuild_context_t *ctx, target_t *target,
                       const char *soname);
void cbuild_set_build_type(cbuild_context_t *ctx, const char *build_type);
void cbuild_add_global_cflags(cbuild_context_t *ctx, const char *flags);
void cbuild_add_global_ldflags(cbuild_context_t *ctx, const char *flags);
void cbuild_add_global_define(cbuild_context_t *ctx, const char *macro);
void cbuild_add_global_define_val(cbuild_context_t *ctx, const char *macro,
                                  const char *value);
void cbuild_add_global_flag(cbuild_context_t *ctx, const char *flag, int value);
void cbuild_enable_compile_commands(cbuild_context_t *ctx, int enabled);
void cbuild_guess_compiler(cbuild_context_t *ctx);
void cbuild_set_verbose(cbuild_context_t *ctx, int verbose);
```

Compiler, linker, and archiver strings may include fixed arguments, such as `zig cc -target x86_64-linux-musl`.

Self-rebuild support is available directly or through the macro described below:

```c
int cbuild_self_rebuild_if_needed(cbuild_context_t *ctx,
                                  int argc, char **argv,
                                  const char **sources, int source_count);
```

## Build configurations

```c
config_t *cbuild_config_new(cbuild_context_t *ctx, const char *name);
config_t *cbuild_config_default_debug(cbuild_context_t *ctx);
config_t *cbuild_config_default_release(cbuild_context_t *ctx);
config_t *cbuild_config_default_wasm32(cbuild_context_t *ctx);
void cbuild_config_free(cbuild_context_t *ctx, config_t *config);

void cbuild_set_active_config(cbuild_context_t *ctx, config_t *config);
void cbuild_target_set_config(cbuild_context_t *ctx, target_t *target,
                              config_t *config);
```

Configuration content:

```c
void cbuild_config_add_cflags(cbuild_context_t *, config_t *, const char *);
void cbuild_config_add_ldflags(cbuild_context_t *, config_t *, const char *);
void cbuild_config_add_define(cbuild_context_t *, config_t *, const char *);
void cbuild_config_add_include(cbuild_context_t *, config_t *, const char *);
void cbuild_config_add_libdir(cbuild_context_t *, config_t *, const char *);
void cbuild_config_add_linklib(cbuild_context_t *, config_t *, const char *);
void cbuild_config_set_opt(cbuild_context_t *, config_t *, int level);
void cbuild_config_set_debug(cbuild_context_t *, config_t *, int enabled);
void cbuild_config_set_lto(cbuild_context_t *, config_t *, int mode);
void cbuild_config_set_pic(cbuild_context_t *, config_t *, int enabled);
void cbuild_config_set_warnings(cbuild_context_t *, config_t *, int mode);
void cbuild_config_set_std(cbuild_context_t *, config_t *, const char *standard);
void cbuild_config_set_runtime(cbuild_context_t *, config_t *, const char *runtime);
void cbuild_config_set_output_dir(cbuild_context_t *, config_t *, const char *dir);
void cbuild_config_set_freestanding(cbuild_context_t *, config_t *, int enabled);
void cbuild_config_enable_sanitizers(cbuild_context_t *, config_t *, int mask);
void cbuild_config_disable_sanitizers(cbuild_context_t *, config_t *, int mask);
void cbuild_config_set_compiler(cbuild_context_t *, config_t *, const char *compiler);
void cbuild_config_set_linker(cbuild_context_t *, config_t *, const char *linker);
```

## Commands

```c
int cbuild_run_command(cbuild_context_t *ctx, command_t *cmd);
command_t *cbuild_command(cbuild_context_t *ctx, const char *name,
                          const char *command_line);
command_t *cbuild_command_argv(cbuild_context_t *ctx, const char *name,
                               char **argv, int argc);
command_t *cbuild_command_function(cbuild_context_t *ctx, const char *name,
                                   cbuild_subcommand_callback callback,
                                   void *user_data);

void cbuild_target_add_command(cbuild_context_t *ctx, target_t *target,
                               command_t *cmd);
void cbuild_target_add_post_command(cbuild_context_t *ctx, target_t *target,
                                    command_t *cmd);
void cbuild_command_add_dependency(cbuild_context_t *ctx, command_t *cmd,
                                   command_t *dependency);
```

Command dependencies run before the command that names them; a dependency cycle fails the command with an error. Use argv commands when possible to avoid shell quoting. Shell commands run through the host shell; on Windows CBuild uses PowerShell.

## Subprojects and subcommands

```c
subproject_t *cbuild_add_subproject(cbuild_context_t *ctx, const char *alias,
                                    const char *directory,
                                    const char *cbuild_exe);
target_t *cbuild_subproject_get_target(cbuild_context_t *ctx,
                                       subproject_t *subproject,
                                       const char *target_name);

void cbuild_register_subcommand(cbuild_context_t *ctx, const char *name,
                                target_t *target, const char *command_line,
                                cbuild_subcommand_callback callback,
                                void *user_data);
```

A registered subcommand can use a command line, a callback, or both, and can optionally require a target to be built first. Run it with `--run=NAME`, `-r NAME`, or just its name as the only positional argument (`./cbuild NAME`).

## Custom flags

```c
void cbuild_register_flag(cbuild_context_t *ctx, const char *long_name,
                          char short_name, int takes_value,
                          cbuild_flag_phase_t phase, const char *help,
                          cbuild_flag_callback callback, void *user_data);
void cbuild_register_flag_bool(cbuild_context_t *, const char *, char,
                               cbuild_flag_phase_t, const char *, int *out);
void cbuild_register_flag_int(cbuild_context_t *, const char *, char,
                              cbuild_flag_phase_t, const char *, int *out);
void cbuild_register_flag_str(cbuild_context_t *, const char *, char,
                              cbuild_flag_phase_t, const char *,
                              const char **out);
int cbuild_has_flag(int argc, char **argv, const char *long_name,
                    char short_name);
```

The phases are `CBUILD_FLAG_PRE`, `CBUILD_FLAG_BEFORE_BUILD`, and `CBUILD_FLAG_AFTER_BUILD`. A callback can return `CBUILD_FLAG_EXIT` after handling an early-exit operation successfully.

## Filesystem and wildcard helpers

```c
int cbuild_file_exists(const char *path);
int cbuild_dir_exists(const char *path);
int cbuild_remove_file(const char *path);
int cbuild_remove_dir(const char *path);
int cbuild_get_cwd(char *buffer, long size);
int cbuild_match_wildcard(const char *pattern, const char *string);
int cbuild_expand_wildcard(const char *pattern,
                           char ***files, int *file_count);
int cbuild_expand_wildcard_recursive(const char *directory,
                                     const char *pattern,
                                     char ***files, int *file_count,
                                     int *capacity);
```

## Convenience macros

All current macros take the context explicitly:

```c
CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "cbuild.h");
CBUILD_SOURCES(ctx, target, "src/a.c", "src/b.c");
CBUILD_INCLUDES(ctx, target, "include", "vendor/include");
CBUILD_LIB_DIRS(ctx, target, "vendor/lib");
CBUILD_LINK_LIBS(ctx, target, "m", "pthread");
CBUILD_DEFINES(ctx, target, "DEBUG", "VALUE=1");
CBUILD_SUBPROJECT(ctx, dependency, "vendor/dependency", "./cbuild");
```

Target-definition wrappers are also available:

```c
target_t *app;
CBUILD_EXECUTABLE(ctx, app,
    CBUILD_SOURCES(ctx, app, "src/main.c");
);
```

Equivalent `CBUILD_STATIC_LIBRARY` and `CBUILD_SHARED_LIBRARY` macros are provided.
