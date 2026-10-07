# Test suite

The test suite is intentionally toolchain-free beyond a C/C++ compiler, Python 3, and PowerShell on Windows.

## Coverage

- Declaration-only inclusion from C and C++
- Public version macros
- Context lifecycle, reset, logging, user data, and error reporting
- Filesystem and wildcard helpers
- Build configuration APIs
- Callback and argv commands, including command dependencies
- Programmatic target builds and cleaning
- Executable, static library, shared library, dummy, and generated-file targets
- Parallel builds and no-op incremental builds
- Header dependency and compile-signature invalidation
- `compile_commands.json` generation and JSON parsing
- Target listing/filtering, graph and reverse-dependency output, manifests, custom flags, and subcommands
- Self-rebuilding build programs
- Rebuild-correctness regressions (`tests/regress`, all platforms): paths containing spaces, edits made within a second of the previous build, removed headers, failed compilation, changed flags, deleted intermediate outputs, repeated no-op builds, explicit output paths, generator dependencies, and command dependency cycles
- `--clean` safety: symbolic links and Windows junctions, files outside the build outputs, and output directories that contain the project
- Subproject discovery, build, link, run, and clean behavior through the repository example

## Local use

Linux or macOS:

```sh
CC=cc CXX=c++ tests/run-posix.sh
```

Both platform scripts run `tests/run-regress.py`, which can also be run on its own (`python3 tests/run-regress.py`, honouring `CC` and, on Unix, `CFLAGS`). It works on a scratch copy of `tests/regress` because its scenarios edit and delete sources.

To use another compiler:

```sh
CC=clang CXX=clang++ tests/run-posix.sh
```

Windows, from a Developer PowerShell with MSVC available:

```powershell
$env:CC = "cl"
tests/run-windows.ps1
```

The scripts clean their executables, logs, and build outputs even when a test fails.
