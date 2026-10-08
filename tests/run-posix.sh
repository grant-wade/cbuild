#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}
CXX=${CXX:-c++}
CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -Wpedantic -Werror}
CXXFLAGS=${CXXFLAGS:--std=c++11 -Wall -Wextra -Wpedantic -Werror}

cleanup() {
    rm -f "$ROOT/tests/api_smoke" \
          "$ROOT/tests/api_smoke_split" \
          "$ROOT/tests/amalgamate" \
          "$ROOT/tests/include_smoke.o" \
          "$ROOT/tests/include_smoke_cpp.o" \
          "$ROOT/tests/project/cbuild" \
          "$ROOT/example/cbuild" \
          "$ROOT/example/lib/cbuild" \
          "$ROOT/tests/project"/*.log \
          "$ROOT/example"/*.log
    rm -rf "$ROOT/tests/api-build" \
           "$ROOT/tests/split-build" \
           "$ROOT/tests/project/build" \
           "$ROOT/example/build" \
           "$ROOT/example/lib/build"
}
trap cleanup EXIT INT TERM
cleanup

cd "$ROOT"

# The committed single header must match what tools/amalgamate.c generates from src/.
# shellcheck disable=SC2086
$CC $CFLAGS tools/amalgamate.c -o tests/amalgamate
./tests/amalgamate --check

# Public declarations must compile cleanly from both C and C++.
# shellcheck disable=SC2086
$CC $CFLAGS -c tests/include_smoke.c -o tests/include_smoke.o
# shellcheck disable=SC2086
$CXX $CXXFLAGS -c tests/include_smoke.cpp -o tests/include_smoke_cpp.o

# Exercise context lifecycle, logging, helpers, commands, configs, and the
# programmatic build API.
# shellcheck disable=SC2086
$CC $CFLAGS tests/api_smoke.c -o tests/api_smoke
./tests/api_smoke

# The files in src/ must also build as ordinary separate translation units.
mkdir -p tests/split-build
for src in src/*.c; do
    # shellcheck disable=SC2086
    $CC $CFLAGS -c "$src" -o "tests/split-build/$(basename "$src" .c).o"
done
# shellcheck disable=SC2086
$CC $CFLAGS -DCBUILD_TEST_SPLIT_SOURCES tests/api_smoke.c tests/split-build/*.o -o tests/api_smoke_split
./tests/api_smoke_split

cd "$ROOT/tests/project"
# shellcheck disable=SC2086
$CC $CFLAGS build.c -o cbuild

./cbuild --version > build-version.log
grep -qx "v0.1.2" build-version.log
./cbuild --help > build-help.log
./cbuild --list > build-list.log
grep -q "test_app" build-list.log
grep -q "shared" build-list.log
grep -q "generated_header" build-list.log

./cbuild --graph > build-graph.log
grep -q "math" build-graph.log
grep -q "generated_header" build-graph.log

./cbuild --deps=math > build-deps.log
grep -q "test_app" build-deps.log

./cbuild --manifest > build-manifest.log
grep -q "static_lib math" build-manifest.log
grep -q "shared_lib shared" build-manifest.log
grep -q "executable test_app" build-manifest.log

./cbuild --compile-commands -j2 > first-build.log

test -f build/generated.h
test -f build/command-dependency.txt
test -f build/command-main.txt
test -f build/test_app
test -f build/libmath.a
case "$(uname -s)" in
    Darwin) test -f build/libshared.dylib ;;
    *)      test -f build/libshared.so ;;
esac

./build/test_app --self-test > app-output.log
grep -q "sum=5 product=12 generated=7 feature=0" app-output.log

python3 - <<'PY'
import json
from pathlib import Path
entries = json.loads(Path("build/compile_commands.json").read_text())
assert len(entries) >= 4, entries
assert all("arguments" in entry and entry["arguments"] for entry in entries)
PY

# A no-op build must not compile or link anything.
./cbuild -j2 > noop-build.log
if grep -E "COMPILE|LINK" noop-build.log; then
    echo "No-op build unexpectedly compiled or linked a target" >&2
    exit 1
fi

# Changing a command-line define must invalidate the compile signature.
./cbuild --feature --target=test_app > feature-build.log
grep -q "COMPILE" feature-build.log
./build/test_app --self-test > feature-output.log
grep -q "feature=1" feature-output.log

./cbuild --feature --run=app > subcommand.log
grep -q "feature=1" subcommand.log

# A bare subcommand name is shorthand for --run=NAME; anything else is rejected.
./cbuild --feature app > positional-subcommand.log
grep -q "feature=1" positional-subcommand.log
if ./cbuild no-such-subcommand > positional-unknown.log 2>&1; then
    echo "Unknown positional argument was unexpectedly accepted" >&2
    exit 1
fi
grep -q "unexpected argument" positional-unknown.log

# A newer generated-file input must rerun generation and rebuild its consumer.
sleep 1
touch inputs/schema.txt
./cbuild --target=test_app > generated-rebuild.log
grep -q "FILE_DEP" generated-rebuild.log
grep -q "COMPILE" generated-rebuild.log

# Verify self-rebuilding build programs.
sleep 1
touch build.c
./cbuild --list > self-rebuild.log
grep -q "Detected changes" self-rebuild.log

./cbuild --clean > clean.log
test ! -e build/test_app

# Rebuild-correctness and clean-safety regressions run on their own scratch copy.
python3 "$ROOT/tests/run-regress.py"

# Keep the repository's subproject example healthy too.
cd "$ROOT/example"
# shellcheck disable=SC2086
$CC $CFLAGS build.c -o cbuild
./cbuild > example-build.log
./cbuild --run=run > example-run.log
grep -q "2 + 3 = 5" example-run.log
./cbuild --clean > example-clean.log
