# Test suite

The test suite needs only a C and C++ compiler. It is built and run by `build.c` in the repository root.

Linux, macOS, or FreeBSD:

```sh
cc -std=c11 build.c -o cbuild
./cbuild --test
```

Windows, from a Developer Command Prompt:

```bat
cl /nologo /std:c11 /D_CRT_SECURE_NO_WARNINGS build.c /Fe:cbuild.exe
.\cbuild.exe --test
```

| Command | Effect |
|---|---|
| `./cbuild amalgamate` | Regenerate `cbuild.h` from `src/` |
| `./cbuild --target=check_header` | Fail if `cbuild.h` does not match `src/` |
| `./cbuild --test` | Run the tests |
| `./cbuild` | Build the tools, suites, and fixture programs |
| `./cbuild install` | Install the launcher to `~/.local/bin`, or `~/bin` on FreeBSD |
| `./cbuild --clean` | Remove `build/` |

`./cbuild` rebuilds itself when `build.c`, `cbuild.h`, or `tests/suite/scenarios.h` change. `--test` stops before running tests when `cbuild.h` does not match `src/`.

`CC`, `CXX`, `CFLAGS`, `CXXFLAGS`, `CPPFLAGS`, and `LDFLAGS` apply to the suite and everything it builds. Sanitizer runs set the sanitizer flags in both `CFLAGS` and `LDFLAGS`. `CC_FOR_BUILD` overrides `CC` when `./cbuild` rebuilds itself.

## Selecting tests

Each scenario is one test named `group.scenario`, with its group as a label.

```sh
./cbuild --list-tests
./cbuild --test --test-label=regress
./cbuild --test --test-filter='runner.*timeout*'
```

Logs are in `build/tests/`. Each scenario works in `build/test work/<test name>/`, which is deleted when it passes and kept when it fails.

## Layout

- `build.c` builds the suite and registers its tests.
- `tests/suite/scenarios.h` lists every scenario.
- `tests/suite/harness.c` has the process, file, and output helpers; `json.c` reads `compile_commands.json`.
- `tests/suite/<group>.c` holds the scenarios of a group.
- `tests/fixtures/` holds the projects scenarios build.

## Groups

| Label | Covers |
|---|---|
| `header` | The declarations in `cbuild.h` compile as C and C++ |
| `api` | The public API called in process: logging, filesystem and wildcard helpers, flag parsing, commands, configurations, reset, listings, test registration, `cbuild_spawn`, and `cbuild_run_tests` |
| `split` | The `api` scenarios against `src/` built as separate translation units |
| `regress` | Rebuild correctness and clean safety: paths with spaces, sub-second edits, headers, failed compilation, changed flags, environment flags, deleted outputs, generators, command cycles, links, and junctions |
| `runner` | The test framework: selection, results, logs, environment, standard input, arguments, timeouts, process trees, signals, parallelism, locks, build dependencies, flag phases, output sections, and color |
| `project` | The command line against a project with every target kind: listings, manifests, outputs, `compile_commands.json`, flags, subcommands, generated files, self-rebuild and its compiler choice and failures, registered tests, and clean |
| `subproject` | Parent projects building and linking child targets, and child output that is not part of the manifest |
| `example` | The repository example: nested builds, rebuilds, failures, clean, run, and tests |
| `launcher` | The `cbuild` launcher: finding projects, the first compile, forwarding, its own flags, and install |

## Adding a scenario

Add `X(group, name)` to `tests/suite/scenarios.h` and define it in `tests/suite/<group>.c`:

```c
SCENARIO(regress, header_edits_rebuild_their_includers) {
    BUILD();
    settle();
    touch_file("space dir/include/spaced.h");
    EXPECT_STEPS(BUILD(), "COMPILE", 2, NULL);
}
```

A scenario starts in a fresh copy of its group's fixture, sets what it needs, and fails by exiting through a `CHECK` or `EXPECT_*`. `h_skip` skips it on platforms where it does not apply.

On Windows, `launcher` scenarios set `CBUILD_LAUNCHER_ENV_KEY` to a scratch key under `HKCU`, which install writes in place of `HKCU\Environment`.
