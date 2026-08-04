# Releasing CBuild

CBuild releases are automatic and version-driven. After a successful push to `main`, the workflow checks whether a GitHub Release matching `CBUILD_VERSION` already exists. It publishes only unreleased header versions. Ordinary code and documentation changes do not create another release once the current version has been published.

A release is published only after the Linux GCC, Linux Clang, macOS Clang, Linux sanitizer, and Windows MSVC jobs all pass.

## Version sources

The canonical public version is declared in `cbuild.h`:

```c
#define CBUILD_VERSION "v0.1.0"
#define CBUILD_VERSION_MAJOR 0
#define CBUILD_VERSION_MINOR 1
#define CBUILD_VERSION_PATCH 0
```

The workflow requires semantic version format with a leading `v`, and verifies that the numeric macros agree with `CBUILD_VERSION`.

## Release checklist

1. Update all four version macros in `cbuild.h`.
2. Update version assertions in `tests/api_smoke.c`, `tests/include_smoke.c`, and `tests/include_smoke.cpp`.
3. Update the current version shown in `readme.md`.
4. Add the new version and release notes to `CHANGELOG.md`.
5. Open or update a pull request and wait for every CI job to pass.
6. Merge the version change to `main`.

The successful `main` workflow then:

1. Validates semantic-version syntax and numeric macro consistency.
2. Checks GitHub for an existing release matching `CBUILD_VERSION`.
3. Packages the single header and documentation when that version is unreleased.
4. Creates an annotated tag matching `CBUILD_VERSION` on the tested commit.
5. Creates the matching GitHub release with generated release notes.

The release assets are:

- `cbuild.h`
- `cbuild-vX.Y.Z.tar.gz`
- `cbuild-vX.Y.Z.zip`
- `SHA256SUMS`

The archives contain `cbuild.h`, the license and author files, the README, and the API reference.

## Safety behavior

- Once a release matching `CBUILD_VERSION` exists, later pushes with that same version skip packaging, tagging, and release creation.
- If platform tests fail, no tag or release is created. A later successful push automatically retries the still-unreleased header version, even when that fix does not change `CBUILD_VERSION` again.
- Pull requests and manual workflow runs test the project but never publish releases.
- A pre-existing version tag is accepted only when it points to the exact commit being released.
- If tagging succeeds but release creation fails, re-running that workflow reuses the same tag and retries release creation.
- Release jobs are not cancelled by newer pushes, so a subsequent `main` update cannot interrupt publishing a tested version.
