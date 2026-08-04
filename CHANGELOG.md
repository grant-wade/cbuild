# Changelog

All notable changes to CBuild are documented here. This project follows [Semantic Versioning](https://semver.org/).

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

[v0.1.0]: https://github.com/grant-wade/cbuild/releases/tag/v0.1.0
