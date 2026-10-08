# CBuild

[![CI and release](https://github.com/grant-wade/cbuild/actions/workflows/ci.yml/badge.svg)](https://github.com/grant-wade/cbuild/actions/workflows/ci.yml)

**Current version: `v0.1.2`**

A build system for C that is just a C header. You describe your build in a `build.c`, compile it with the compiler you already have, and run it. No Makefile dialect, no configuration language, nothing else to install.

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

Because the build is a real program, anything C can do is fair game: loops over source lists, `#ifdef` for platform quirks, helper functions, your own libraries.

## Try it

Grab `cbuild.h` from the [latest release](https://github.com/grant-wade/cbuild/releases/latest) (or copy it out of this repo), drop it next to a `build.c` like the one above, and:

```sh
cc -o cbuild build.c
./cbuild
```

On Windows, from a Developer Command Prompt:

```bat
cl /nologo /Fe:cbuild.exe build.c
cbuild.exe
```

You only compile it by hand once. `CBUILD_SELF_REBUILD` notices when `build.c` (or anything else you list, primary source first) has changed, then recompiles and restarts itself.

## What you get

- **Incremental, parallel builds.** Only what changed gets rebuilt, based on timestamps, header dependencies, and the exact compile and link commands, so changing a flag rebuilds what it should. Compiles run across all your cores, or as many as you pass to `-j`.
- **GCC, Clang, and MSVC** on Linux, macOS, and Windows, with output names that follow each platform (`libfoo.a` vs `foo.lib`, `.so` vs `.dylib` vs `.dll`).
- **Executables, static and shared libraries**, plus generated files and custom commands that slot into the same dependency graph.
- **Build configurations** for Debug, Release, WebAssembly, or your own, written once instead of per compiler.
- **Subprojects**: link against a target from another CBuild project.
- **`compile_commands.json`** for clangd and friends.
- **No global state.** Everything hangs off a context, so you can also embed CBuild as a library and skip the CLI.

## Running your build

```sh
./cbuild                      # build everything
./cbuild -t app               # build one target and what it needs
./cbuild -j 8                 # choose the number of parallel jobs
./cbuild -v                   # show the full compiler and linker commands
./cbuild --clean              # remove outputs, including subprojects'
./cbuild NAME                 # build and run a subcommand you registered (same as --run=NAME)
./cbuild --compile-commands   # write compile_commands.json
./cbuild --list               # list targets
./cbuild --graph              # print the build graph
./cbuild --deps=NAME          # show what depends on NAME
./cbuild --help               # everything, including flags your build adds
```

The only bare word CBuild accepts is the name of a subcommand you registered. Everything built in is an option: it's `./cbuild --clean`, not `./cbuild clean`.

## A quick tour

### Targets

```c
target_t *core = cbuild_static_library(ctx, "core");
CBUILD_SOURCES(ctx, core, "src/core/*.c", "vendor/**/*.c");
CBUILD_INCLUDES(ctx, core, "include", "vendor/include");
CBUILD_DEFINES(ctx, core, "PROJECT_INTERNAL", "HAVE_FEATURE=1");

target_t *app = cbuild_executable(ctx, "app");
cbuild_add_source(ctx, app, "src/main.c");
cbuild_add_link_target(ctx, app, core);   /* depend on and link our own library */
cbuild_add_link_library(ctx, app, "m");   /* link a system library */
```

Wildcards work, including recursive `**`. If you don't like the default output name, `cbuild_set_output_file` overrides it.

### Configurations

A configuration is a bundle of compiler settings described portably, so you say "optimise, with debug info" and CBuild picks the right flags for GCC, Clang, or MSVC.

```c
config_t *release = cbuild_config_default_release(ctx);
cbuild_set_active_config(ctx, release);

config_t *tools = cbuild_config_new(ctx, "Tools");
cbuild_config_set_std(ctx, tools, "c11");
cbuild_config_set_opt(ctx, tools, 0);
cbuild_config_set_debug(ctx, tools, 1);
cbuild_config_set_warnings(ctx, tools, 2);
cbuild_config_set_output_dir(ctx, tools, "build/tools");

cbuild_target_set_config(ctx, generator, tools);   /* just this target */
```

There are also knobs for LTO, PIC, sanitizers, the runtime, freestanding mode, and the compiler and linker themselves. If all you need is the basics, `cbuild_set_build_type(ctx, "Debug")` or `"Release"` does it in one line.

### Commands and generated files

Commands can be a shell string, an argv array, or a C callback, and can run before or after a target:

```c
command_t *generate = cbuild_command(
    ctx, "generate version", "python3 tools/version.py > build/version.h");

cbuild_target_add_command(ctx, app, generate);       /* before target */
cbuild_target_add_post_command(ctx, app, package);   /* after target */
cbuild_command_add_dependency(ctx, package, generate);
```

For a file you generate, use a file-dependency target. It reruns only when the output is missing or an input is newer:

```c
target_t *generated = cbuild_file_dep_target(
    ctx, "generated_header", "build/generated.h");
cbuild_add_source(ctx, generated, "schema.json");
cbuild_target_add_command(ctx, generated, generate);
cbuild_add_link_target(ctx, app, generated);
```

And for the things you'd otherwise put in a `run` or `test` script, register a subcommand and call it with `./cbuild run`:

```c
cbuild_register_subcommand(ctx, "run", app, "./build/app", NULL, NULL);
```

### Your own flags

```c
int enable_tests = 0;
cbuild_register_flag_bool(
    ctx, "tests", 0, CBUILD_FLAG_PRE,
    "Build test targets", &enable_tests);
```

That gives you `./cbuild --tests`, and it shows up in `--help`. Integer, string, and callback flags work the same way.

### Subprojects

A subproject is another CBuild project with its own build program:

```c
subproject_t *math_project =
    cbuild_add_subproject(ctx, "math", "vendor/math", "./cbuild");
target_t *math =
    cbuild_subproject_get_target(ctx, math_project, "math");
cbuild_add_link_target(ctx, app, math);
```

CBuild asks the subproject what it offers, builds it when needed, and links the result. The subproject's build program has to exist first; [`example/build.c`](example/build.c) shows a small command that bootstraps it.

### Without the CLI

`cbuild_run` is a convenience. You can drive everything yourself:

```c
int result = cbuild_build(ctx, NULL);       /* all targets */
int result = cbuild_build(ctx, "app");      /* one target */
int clean_result = cbuild_clean(ctx);
```

Errors come back through `cbuild_get_last_error`, and `cbuild_set_logger` sends output wherever you want it.

## Going deeper

- [`docs/api.md`](docs/api.md) is the full API reference.
- [`example/`](example/) is a small project that links against a subproject.
- [`tests/`](tests/) explains the test suite and how to run it.
- [`CHANGELOG.md`](CHANGELOG.md) has what changed in each version.

## Hacking on CBuild

`cbuild.h` is a generated file, so don't edit it. The real code is in [`src/`](src/) as ordinary C files, and [`tools/amalgamate.c`](tools/amalgamate.c) stitches them into the single header:

```sh
cc tools/amalgamate.c -o amalgamate
./amalgamate            # rewrite cbuild.h from src/
./amalgamate --check    # exit 1 if cbuild.h is out of date
```

You shouldn't need to run that by hand. Turn on the pre-commit hook once per clone and it regenerates and stages `cbuild.h` on every commit:

```sh
git config core.hooksPath .githooks
```

The hook needs a C compiler on your `PATH` (or in `CC`). If a stale header slips through anyway, CI will catch it.

A few things worth knowing when you're in `src/`:

- `src/cbuild.h` is the public API and `src/cbuild_internal.h` holds the private types and shared helpers.
- A helper used by more than one file is declared in `cbuild_internal.h` with `CBUILD_INTERNAL` instead of `static`. It still ends up `static` in the generated header.
- New `.c` and `.h` files in `src/` are picked up automatically.

Run the tests with `tests/run-posix.sh` (or `tests/run-windows.ps1`). Releases are automatic and described in [`docs/releasing.md`](docs/releasing.md).

## License

[BSD 3-Clause](LICENSE).

## Acknowledgments

CBuild owes ideas to [nob.h](https://github.com/tsoding/nob.h) and [tup](https://github.com/gittup/tup).
