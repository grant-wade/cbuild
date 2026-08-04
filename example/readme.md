# CBuild subproject example

This example builds a `math` static library in `lib/` as a separate CBuild subproject, then links it into the top-level `main` executable.

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
- `build/compile_commands.json`

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
```
