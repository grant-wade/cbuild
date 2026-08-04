# Test suite

The test suite is intentionally toolchain-free beyond a C/C++ compiler, Python 3 for JSON validation on Unix, and PowerShell on Windows.

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
- Subproject discovery, build, link, run, and clean behavior through the repository example

## Local use

Linux or macOS:

```sh
CC=cc CXX=c++ tests/run-posix.sh
```

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
