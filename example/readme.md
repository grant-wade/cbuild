# CBuild subproject example

This example builds a `math` static library in `lib/` as a separate CBuild subproject, then links it into the top-level `main` executable and the `test_math` test program.

The top-level build program automatically compiles `lib/build.c` the first time it is needed. Both build programs use the context-based API and self-rebuild when their source or the shared `cbuild.h` changes.

## Build and run

From this directory:

```sh
cc -o cbuild build.c
./cbuild
./cbuild --run=run
```

The outputs are:

- `lib/build/libmath.a` (or `lib/build/math.lib` on Windows)
- `build/main` (or `build/main.exe` on Windows)
- `build/test_math` (or `build/test_math.exe` on Windows)
- `build/compile_commands.json`

## Test

```sh
./cbuild --test
./cbuild --list-tests
./cbuild --test --test-label=unit
./cbuild --test --test-filter='math_*' --test-exclude=math_power
MATH_SLOW_TESTS=1 ./cbuild --test --test-label=slow
```

| Test | Label | Runs |
| --- | --- | --- |
| `math_arithmetic`, `math_division`, `math_power` | `unit` | `test_math` with the suite name as its argument |
| `math_round_trips` | `slow` | `test_math round_trips`, which exits 77 to skip unless `MATH_SLOW_TESTS` is set, with a 60 second timeout |
| `main_runs` | `smoke` | `main` |

Each test's output is in `build/tests/<name>.log`.

## Inspect

Useful inspection commands:

```sh
./cbuild --list
./cbuild --graph
./cbuild --deps=math_project_math
./cbuild --target=main
./cbuild --clean
```

With MSVC from a Developer Command Prompt:

```bat
cl /nologo /Fe:cbuild.exe build.c
cbuild.exe
cbuild.exe --run=run
cbuild.exe --test
```
