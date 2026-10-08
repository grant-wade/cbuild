# Changelog

All notable changes to CBuild are documented here. This project follows [Semantic Versioning](https://semver.org/).

## [v0.1.2] - 2026-10-07

### Added

- A registered subcommand can be run by name: `./cbuild run` is shorthand for `./cbuild --run=run`.

### Changed

- The implementation now lives in `src/` as separate C source files. `cbuild.h` is generated from them by `tools/amalgamate.c`, kept current by a pre-commit hook, and verified in CI. The public API and behaviour are unchanged.

## [v0.1.1] - 2026-10-07

Correctness release: CBuild should not skip a necessary rebuild, rebuild unchanged sources, or delete files it did not produce.

### Changed

- Change detection compares file timestamps at the full resolution the platform records (nanoseconds on Linux and macOS, 100ns on Windows) instead of whole seconds, so an edit made within the same second as the previous build is no longer missed.
- The regression suite in `tests/regress` runs on Linux, macOS, and Windows through a shared Python driver.

### Fixed

- A failed compile removes the stale object file and its signature, and on MSVC no longer replaces the recorded header list with a partial one, so restoring an older file cannot make the failure look up to date.
- MSVC compile errors are printed in full instead of being cut off after the first line.
- GCC-style compilers on Windows keep their `-MMD` dependency file instead of having it overwritten in the MSVC format.

- `cbuild_set_output_file` paths are no longer overwritten with the default output path when the build starts.
- GCC/Clang dependency files with escaped spaces (and `\#`, `$$`, or `-MP` phony rules) are parsed correctly, so sources under paths containing spaces stop recompiling on every build.
- Circular command dependencies are reported as an error instead of recursing until the stack overflows.
- `--clean` never follows symbolic links, refuses to sweep an output directory that is or contains the working directory, and leaves file-dependency targets without commands (files CBuild did not create) in place. It also removes the link signature next to outputs placed outside the output directory.
- `compile_commands.json` uses each target's effective compiler and flags, reflects `BEFORE_BUILD` flag callbacks, and no longer lists the inputs of generated-file targets as translation units.
- Generated-file targets build their own target dependencies first and regenerate when one of them is rebuilt.
- A target relinks whenever a dependency was rebuilt in the same build, even when both share a timestamp on filesystems with coarse resolution.
- Long include paths and defines are no longer silently truncated on the compiler command line.

## [v0.1.0] - 2026-08-03

Initial versioned release of the modern CBuild implementation.

### Added

- Context-based API with explicit lifecycle, error reporting, user data, and custom logging.
- Executable, static/shared library, dummy, generated-file, and subproject targets.
- Parallel compilation and incremental dependency tracking for GCC, Clang, and MSVC.
- Compile and link signatures that invalidate outputs when flags or environment settings change.
- Structured build configurations with optimization, debugging, warnings, PIC, LTO, sanitizers, runtime, and toolchain controls.
- Shell, argv, and callback commands with dependencies and pre/post target hooks.
- Named subcommands, custom CLI flags, target filtering, graph inspection, and manifest output.
- Recursive wildcard expansion and `compile_commands.json` generation.
- Self-rebuilding build programs and public semantic-version macros with `--version` CLI output.
- Cross-platform CI for Linux, macOS, and Windows, including sanitizer coverage.
- Automated, version-validated GitHub releases when `CBUILD_VERSION` changes on `main`.

### Fixed

- Subproject proxy targets now preserve and link their externally produced outputs.
- Strict C11 builds no longer depend on implicit POSIX declarations.
- Platform-specific object extensions no longer trigger macro redefinition warnings.
- Successful recompiles always force relinking, even on filesystems with coarse timestamp resolution.
- Parallel compilation no longer depends on unnamed POSIX semaphores, which are unavailable on macOS.
- macOS CPU detection now uses `sysctlbyname` instead of unavailable `_SC_NPROCESSORS_ONLN` declarations.
- Windows subproject commands now use PowerShell-compatible directory changes and invocation.
- MSVC dependency tracking preserves header paths containing spaces, preventing perpetual no-op recompiles.

[v0.1.2]: https://github.com/grant-wade/cbuild/releases/tag/v0.1.2
[v0.1.1]: https://github.com/grant-wade/cbuild/releases/tag/v0.1.1
[v0.1.0]: https://github.com/grant-wade/cbuild/releases/tag/v0.1.0
