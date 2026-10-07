#!/usr/bin/env python3
"""Rebuild-correctness and clean-safety regressions for every platform.

Works on a scratch copy of tests/regress whose path contains a space, because
the scenarios edit and delete sources. Edits are separated from the previous
build by a fraction of a second only, so the run also proves that change
detection does not depend on whole-second timestamps.
"""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""
ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "tests" / "regress work"
CC = os.environ.get("CC") or ("cl" if WINDOWS else "cc")

APP = Path("dist") / ("regress_app" + EXE)
LIBRARY = Path("build") / ("util.lib" if WINDOWS else "libutil.a")
STEP_LABELS = ("COMPILE", "LINK", "FILE_DEP", "COMMAND")
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


class Failure(Exception):
    pass


def cbuild(*args, env=None):
    """Run the build program; returns (exit status, combined output)."""
    full_env = dict(os.environ)
    full_env.update(env or {})
    result = subprocess.run(
        [str(WORK / ("cbuild" + EXE)), *args], cwd=WORK, env=full_env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        encoding="utf-8", errors="replace")
    return result.returncode, result.stdout


def build(*args, env=None):
    status, log = cbuild(*args, env=env)
    if status != 0:
        raise Failure(f"build {args} {env or ''} failed with status {status}:\n{log}")
    return log


def steps(log, label, mentioning=""):
    """Count the build steps of one kind, optionally only those naming a path."""
    pattern = re.compile(rf"\b{label}\b")
    return sum(1 for line in ANSI_ESCAPE.sub("", log).splitlines()
               if pattern.search(line) and mentioning in line)


def expect_steps(what, log, label, count, mentioning=""):
    actual = steps(log, label, mentioning)
    if actual != count:
        raise Failure(f"{what}: expected {count} {label} step(s) "
                      f"{mentioning}, saw {actual}:\n{log}")


def expect_noop(what, env=None):
    log = build(env=env)
    for label in STEP_LABELS:
        if steps(log, label):
            raise Failure(f"{what}: an up-to-date build did work:\n{log}")


def expect_build_failure(what):
    status, log = cbuild()
    if status == 0:
        raise Failure(f"{what}: build succeeded but had to fail:\n{log}")


def expect_app_output(expected):
    actual = subprocess.run([str(WORK / APP)], cwd=WORK, stdout=subprocess.PIPE,
                            encoding="utf-8", check=True).stdout.strip()
    if actual != expected:
        raise Failure(f"app printed {actual!r}, expected {expected!r}")


def check(condition, message):
    if not condition:
        raise Failure(message)


def settle():
    """Let the clock move past the previous build's timestamps.

    Far shorter than a second on purpose, but longer than the coarsest clock
    tick filesystems stamp files with (about 16ms on Windows).
    """
    time.sleep(0.1)


def path(relative):
    return WORK / relative


def read(relative):
    return path(relative).read_text(encoding="utf-8")


def write(relative, text):
    path(relative).write_text(text, encoding="utf-8", newline="\n")


def touch(relative):
    os.utime(path(relative), None)


def build_outputs(directory, kind):
    """Files in an object directory: 'object', 'depfile', or 'signature'."""
    suffix = {"depfile": ".d", "signature": ".sig"}.get(kind)
    found = []
    for entry in sorted(path(directory).iterdir()):
        if suffix:
            if entry.name.endswith(suffix):
                found.append(entry)
        elif entry.suffix in (".o", ".obj"):
            found.append(entry)
    check(found, f"no {kind} files in {directory}")
    return found


def link_directory(link, target):
    """Directory link that needs no privileges: a junction on Windows."""
    if WINDOWS:
        import _winapi
        _winapi.CreateJunction(str(path(target)), str(path(link)))
    else:
        os.symlink(os.path.relpath(path(target), path(link).parent), path(link))


def remove_link(link):
    if WINDOWS:
        os.rmdir(path(link))
    else:
        os.unlink(path(link))


def compile_build_program():
    if WINDOWS:
        command = [CC, "/nologo", "/std:c11", "/W4", "/D_CRT_SECURE_NO_WARNINGS",
                   f"/I{ROOT}", "build.c", "/Fe:cbuild.exe"]
    else:
        flags = os.environ.get("CFLAGS") or "-std=c11 -Wall -Wextra -Wpedantic -Werror"
        command = [*shlex.split(CC), *shlex.split(flags), f"-I{ROOT}",
                   "build.c", "-o", "cbuild"]
    subprocess.run(command, cwd=WORK, check=True)


def generated_files_respect_dependencies():
    # Building just the app from a clean tree needs the generator built first.
    build("--target=regress_app")
    check(path("build/gen" + EXE).is_file(), "generator was not built")
    check(path("build/generated.h").is_file(), "header was not generated")


def explicit_output_paths():
    check(path(APP).is_file(), f"{APP} was not produced")
    check(not path("build/regress_app" + EXE).exists(),
          "explicit output path was ignored")
    expect_app_output("generated=41 util=7 mode=0")


def compile_commands_use_target_settings():
    build("--compile-commands", "-j2")
    entries = json.loads(read("build/compile_commands.json"))
    by_file = {entry["file"]: entry["arguments"] for entry in entries}
    check(sorted(by_file) == ["space dir/src/util.c", "src/app.c", "src/gen.c"],
          f"unexpected compile_commands.json entries: {sorted(by_file)}")
    compiler = shlex.split(CC)
    for arguments in by_file.values():
        check(arguments[:len(compiler)] == compiler, f"wrong compiler: {arguments}")
    check("-DTARGET_CC=1" in by_file["space dir/src/util.c"],
          "per-target compiler flags are missing")
    check("-DTARGET_CC=1" not in by_file["src/app.c"],
          "per-target compiler flags leaked into another target")
    check(any(argument.endswith("Ispace dir/include")
              for argument in by_file["src/app.c"]), "include directory is missing")


def repeated_builds_are_noops():
    # Includes dependency paths with spaces, which used to defeat the check.
    for round_number in (1, 2, 3):
        expect_noop(f"repeated build {round_number}")


def edits_within_the_same_second_are_noticed():
    started = time.time()
    settle()
    write("src/app.c", read("src/app.c") + "/* edited */\n")
    log = build()
    expect_steps("sub-second source edit", log, "COMPILE", 1, "app.c")
    settle()
    touch("space dir/include/extra.h")
    log = build()
    expect_steps("sub-second header edit", log, "COMPILE", 1, "util.c")
    expect_steps("sub-second header edit", log, "COMPILE", 1)
    if time.time() - started >= 1.0:
        print("regress: note: machine too slow to exercise sub-second edits")


def header_edits_rebuild_their_includers():
    settle()
    touch("space dir/include/spaced.h")
    expect_steps("shared header edit", build(), "COMPILE", 2)


def removed_headers():
    # Still included: the build has to notice and fail, every time.
    os.replace(path("space dir/include/extra.h"), path("extra.h.saved"))
    expect_build_failure("missing header")
    expect_build_failure("missing header, second attempt")
    os.replace(path("extra.h.saved"), path("space dir/include/extra.h"))
    expect_steps("restored header", build(), "COMPILE", 1)

    # No longer included: the stale dependency entry must not break or loop.
    settle()
    lines = read("space dir/src/util.c").splitlines(keepends=True)
    write("space dir/src/util.c",
          "".join(line for line in lines if '"extra.h"' not in line))
    path("space dir/include/extra.h").unlink()
    expect_steps("dropped header", build(), "COMPILE", 1)
    expect_app_output("generated=41 util=3 mode=0")
    expect_noop("after dropping a header")


def failed_compilation():
    settle()
    original = read("src/app.c")
    write("src/app.c", original + "#error broken on purpose\n")
    expect_build_failure("broken source")
    expect_build_failure("broken source, second attempt")
    settle()
    write("src/app.c", original)
    log = build()
    expect_steps("fixed source", log, "COMPILE", 1)
    expect_steps("fixed source", log, "LINK", 1)
    expect_app_output("generated=41 util=3 mode=0")


def changing_flags():
    define = {"REGRESS_APP_DEFINE": "MODE=2"}
    log = build(env=define)
    expect_steps("added define", log, "COMPILE", 1, "app.c")
    expect_steps("added define", log, "COMPILE", 1)
    expect_app_output("generated=41 util=3 mode=2")
    expect_noop("unchanged define", env=define)
    expect_steps("removed define", build(), "COMPILE", 1)

    log = build(env={"REGRESS_UTIL_CFLAGS": "-O1"})
    expect_steps("added cflags", log, "COMPILE", 1, "util.c")
    expect_steps("added cflags", log, "COMPILE", 1)
    expect_steps("added cflags", log, "LINK", 1, "regress_app")
    expect_steps("removed cflags", build(), "COMPILE", 1)


def deleted_intermediates_come_back():
    for entry in build_outputs("build/obj_util", "object"):
        entry.unlink()
    expect_steps("deleted object", build(), "COMPILE", 1)

    for entry in build_outputs("build/obj_regress_app", "depfile"):
        entry.unlink()
    expect_steps("deleted dependency file", build(), "COMPILE", 1)

    for entry in build_outputs("build/obj_regress_app", "signature"):
        entry.unlink()
    expect_steps("deleted signature", build(), "COMPILE", 1)

    path(LIBRARY).unlink()
    expect_steps("deleted library", build(), "COMPILE", 0)
    check(path(LIBRARY).is_file(), "library was not relinked")

    path(APP).unlink()
    log = build()
    expect_steps("deleted executable", log, "COMPILE", 0)
    expect_steps("deleted executable", log, "LINK", 1)
    check(path(APP).is_file(), "executable was not relinked")

    path("build/generated.h").unlink()
    expect_steps("deleted generated file", build(), "FILE_DEP", 1)
    check(path("build/generated.h").is_file(), "header was not regenerated")
    expect_noop("after restoring deleted outputs")


def rebuilt_generator_regenerates():
    settle()
    write("src/gen.c", read("src/gen.c").replace("GENERATED_VALUE 41",
                                                 "GENERATED_VALUE 42"))
    log = build()
    expect_steps("generator edit", log, "COMPILE", 1, "gen.c")
    expect_steps("generator edit", log, "FILE_DEP", 1)
    expect_steps("generator edit", log, "COMPILE", 1, "app.c")
    expect_app_output("generated=42 util=3 mode=0")
    expect_noop("after regenerating")


def command_cycles_are_reported():
    status, log = cbuild("--target=cycle", env={"REGRESS_COMMAND_CYCLE": "1"})
    check(0 < status < 128, f"command cycle exited with status {status}:\n{log}")
    check("circular command dependency" in log, f"cycle was not reported:\n{log}")


def clean_removes_only_outputs():
    path("precious").mkdir()
    write("precious/keep.txt", "keep me\n")
    path("build/nested").mkdir()
    link_directory("build/dirlink", "precious")
    link_directory("build/nested/deeplink", "precious")
    if not WINDOWS:
        os.symlink("../precious/keep.txt", path("build/filelink"))
    build("--clean")
    check(not path("build").exists(), "output directory survived --clean")
    check(not path(APP).exists(), "explicit output survived --clean")
    check(not path(str(APP) + ".link.sig").exists(), "link signature survived --clean")
    check(path("dist/README.txt").is_file(), "--clean removed a file next to an output")
    check(path("vendor/data.txt").is_file(), "--clean removed an input file")
    check(path("precious/keep.txt").is_file(), "--clean followed a link")

    # An output directory that holds the project itself is never swept.
    log = build("--clean", env={"REGRESS_OUTDIR": "."})
    check("not cleaning" in log, f"no refusal reported:\n{log}")
    check(path("build.c").is_file() and path("src/app.c").is_file(),
          "--clean swept the project directory")
    check(path("precious/keep.txt").is_file(), "--clean swept a sibling directory")

    # A linked output directory still loses its build products, but files
    # cbuild does not know about behind the link survive.
    path("real-build").mkdir()
    link_directory("build", "real-build")
    build()
    library_behind_link = path("real-build") / LIBRARY.name
    check(library_behind_link.is_file(), "build through a linked directory failed")
    write("real-build/stray.txt", "stray\n")
    log = build("--clean")
    check("not cleaning" in log, f"no refusal reported:\n{log}")
    check(path("real-build/stray.txt").is_file(), "--clean swept a linked directory")
    check(not library_behind_link.exists(), "library survived --clean")
    check(not path("real-build/obj_util").exists(), "objects survived --clean")
    check(not path(APP).exists(), "explicit output survived --clean")
    remove_link("build")


SCENARIOS = [
    generated_files_respect_dependencies,
    explicit_output_paths,
    compile_commands_use_target_settings,
    repeated_builds_are_noops,
    edits_within_the_same_second_are_noticed,
    header_edits_rebuild_their_includers,
    removed_headers,
    failed_compilation,
    changing_flags,
    deleted_intermediates_come_back,
    rebuilt_generator_regenerates,
    command_cycles_are_reported,
    clean_removes_only_outputs,
]


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        shutil.copytree(ROOT / "tests" / "regress", WORK)
        path("dist").mkdir()
        write("dist/README.txt", "not a build output\n")
        compile_build_program()
        for scenario in SCENARIOS:
            name = scenario.__name__.replace("_", " ")
            try:
                scenario()
            except Failure as failure:
                print(f"regress: FAILED: {name}\n{failure}", file=sys.stderr)
                return 1
            print(f"regress: ok: {name}", flush=True)
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("regress: all scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
