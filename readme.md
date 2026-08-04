# CBuild

CBuild is a cross-platform, single-header build system for C projects. Build descriptions are ordinary C programs, so they can use functions, loops, platform checks, and existing C libraries without a separate configuration language or runtime.

```c
#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (!ctx) return 1;

    CBUILD_SELF_REBUILD(ctx, argc, argv, "build.c", "cbuild.h");
    cbuild_set_output_dir(ctx, "build");

    target_t *util = cbuild_static_library(ctx, "util");
    cbuild_add_source(ctx, util, "src/util.c");
    cbuild_add_include_dir(ctx, util, "include");

    target_t *app = cbuild_executable(ctx, "app");
    cbuild_add_source(ctx, app, "src/main.c");
    cbuild_add_include_dir(ctx, app, "include");
    cbuild_add_link_target(ctx, app, util);

    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
```

## Features

- Executable, static-library, shared-library, file-dependency, and dummy targets
- Incremental builds using timestamps, generated header dependencies, and compile/link signatures
- Parallel compilation with automatic CPU detection or `-j` selection
- GCC, Clang, and MSVC support on Linux, macOS, and Windows
- Per-context and per-target compiler flags, linker flags, definitions, and output paths
- Structured Debug, Release, WebAssembly, and custom build configurations
- Wildcards, including recursive `**` source patterns
- Shell, argv-based, and C callback commands with dependencies and pre/post target hooks
- Named subcommands and custom command-line flags
- Subprojects with manifest-based target discovery
- `compile_commands.json` generation
- Build graph inspection, target selection, and reverse-dependency inspection
- Self-rebuilding build programs
- Context-based API with no global build state, suitable for embedding

CBuild does not use a content-addressed local build cache. Incremental metadata is stored beside build outputs as `.d`, `.sig`, and `.link.sig` files.

## Getting started

Copy `cbuild.h` into a project and create `build.c` using the example above. Define `CBUILD_IMPLEMENTATION` in exactly one translation unit.

Compile the build program:

```sh
cc -o cbuild build.c
```

With MSVC from a Developer Command Prompt:

```bat
cl /nologo /Fe:cbuild.exe build.c
```

Then run it:

```sh
./cbuild
```

The `CBUILD_SELF_REBUILD` call recompiles and restarts the build program when any listed source changes. Put the primary build source first.

## Command-line interface

| Option | Description |
| --- | --- |
| `-h`, `--help` | Show built-in and project-defined options |
| `-v`, `--verbose` | Show full compiler and linker commands |
| `-j N`, `--jobs=N` | Set the number of parallel compile jobs |
| `-t NAME`, `--target=NAME` | Build only a target and its dependencies |
| `--compile-commands` | Write `compile_commands.json` to the active output directory |
| `-l`, `--list` | List targets and exit |
| `--graph` | Print the build graph and exit |
| `--deps=NAME` | Print targets that consume `NAME` and exit |
| `--manifest` | Print the machine-readable target manifest used by subprojects |
| `--clean` | Remove outputs and clean registered subprojects |
| `-r NAME`, `--run=NAME` | Build and execute a registered subcommand |

CBuild uses options rather than the positional commands supported by older releases. For example, use `./cbuild --clean`, not `./cbuild clean`.

## Targets and dependencies

```c
target_t *core = cbuild_static_library(ctx, "core");
CBUILD_SOURCES(ctx, core, "src/core/*.c", "vendor/**/*.c");
CBUILD_INCLUDES(ctx, core, "include", "vendor/include");
CBUILD_DEFINES(ctx, core, "PROJECT_INTERNAL", "HAVE_FEATURE=1");

target_t *app = cbuild_executable(ctx, "app");
cbuild_add_source(ctx, app, "src/main.c");
cbuild_add_link_target(ctx, app, core);
cbuild_add_link_library(ctx, app, "m");
```

`cbuild_add_link_target` adds both a graph dependency and the target's library output to the dependent target's link command. Use `cbuild_add_link_library` for a system or externally supplied library.

Target output names are platform-aware:

- Executable: `build/name` or `build/name.exe`
- Static library: `build/libname.a` or `build/name.lib`
- Shared library: `build/libname.so`, `build/libname.dylib`, or `build/name.dll`

Override them with `cbuild_set_output_file` when necessary.

## Build configurations

Configurations collect portable compiler settings and can be active globally or assigned to individual targets.

```c
config_t *release = cbuild_config_default_release(ctx);
cbuild_set_active_config(ctx, release);

config_t *tools = cbuild_config_new(ctx, "Tools");
cbuild_config_set_std(ctx, tools, "c11");
cbuild_config_set_opt(ctx, tools, 0);
cbuild_config_set_debug(ctx, tools, 1);
cbuild_config_set_warnings(ctx, tools, 2);
cbuild_config_set_output_dir(ctx, tools, "build/tools");
cbuild_config_add_define(ctx, tools, "TOOL_BUILD=1");

cbuild_target_set_config(ctx, generator, tools);
```

Configuration controls include optimization, debug information, LTO, PIC, warnings, language standard, runtime selection, freestanding mode, sanitizers, compiler/linker selection, flags, definitions, includes, library directories, and linked libraries.

For simple projects, `cbuild_set_build_type(ctx, "Debug")` and `cbuild_set_build_type(ctx, "Release")` remain available.

## Commands and generated files

Commands may use a shell string, an argv array, or a C callback:

```c
command_t *generate = cbuild_command(
    ctx, "generate version", "python3 tools/version.py > build/version.h");

cbuild_target_add_command(ctx, app, generate);       /* before target */
cbuild_target_add_post_command(ctx, app, package);   /* after target */
cbuild_command_add_dependency(ctx, package, generate);
```

For generated files, use a file-dependency target so generation runs only when the output is absent or one of its inputs is newer:

```c
target_t *generated = cbuild_file_dep_target(
    ctx, "generated_header", "build/generated.h");
cbuild_add_source(ctx, generated, "schema.json");
cbuild_target_add_command(ctx, generated, generate);
cbuild_add_link_target(ctx, app, generated);
```

Register a named action with an optional build target:

```c
cbuild_register_subcommand(
    ctx, "run", app, "./build/app", NULL, NULL);
```

Invoke it with `./cbuild --run=run`.

## Subprojects

A subproject is another CBuild project with its own compiled build program:

```c
subproject_t *math_project =
    cbuild_add_subproject(ctx, "math", "vendor/math", "./cbuild");
target_t *math =
    cbuild_subproject_get_target(ctx, math_project, "math");
cbuild_add_link_target(ctx, app, math);
```

CBuild enters the subproject directory, requests its `--manifest`, builds it on demand, and links the selected output. The subproject build executable must already exist; see [`example/build.c`](example/build.c) for a small bootstrap command.

## Programmatic and embedded use

The CLI is optional:

```c
int result = cbuild_build(ctx, NULL);       /* all targets */
int result = cbuild_build(ctx, "app");      /* one target */
int clean_result = cbuild_clean(ctx);
```

Use `cbuild_configure_from_argv` to process common options without running a build. Errors are available through `cbuild_get_last_error`, and output can be routed through `cbuild_set_logger`.

A context owns its targets, commands, configurations, and strings. Release it with `cbuild_context_free`. `cbuild_reset` clears a context for reuse; `cbuild_teardown` releases its internal state without freeing the context allocation.

## Custom flags

Projects can extend `--help` and bind options directly:

```c
int enable_tests = 0;
cbuild_register_flag_bool(
    ctx, "tests", 0, CBUILD_FLAG_PRE,
    "Build test targets", &enable_tests);
```

Integer, string, and callback-based handlers are also available. Flag phases allow handlers before the build, immediately before building, or after a successful build.

## Documentation and examples

- [`cbuild.h`](cbuild.h) contains the public declarations and implementation notes.
- [`docs/api.md`](docs/api.md) summarizes the public API.
- [`example/`](example/) demonstrates a main project linked to a CBuild subproject.

## License

CBuild is distributed under the [BSD 3-Clause License](LICENSE).

## Acknowledgments

- [nob.h](https://github.com/tsoding/nob.h)
- [tup](https://github.com/gittup/tup)
