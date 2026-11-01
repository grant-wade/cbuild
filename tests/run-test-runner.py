#!/usr/bin/env python3
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
WORK = ROOT / "tests" / "runner work"
CC = os.environ.get("CC") or ("cl" if WINDOWS else "cc")
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")
HANG_LIMIT_SECONDS = 120
STDIN_FOR_BUILD = "input the tests must not see\n"


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


def cbuild_raw(*args, env=None, cwd=WORK):
    """Run the build program in cwd; returns (exit status, combined output)."""
    shutil.rmtree(WORK / "sync", ignore_errors=True)
    (WORK / "sync").mkdir()
    full_env = dict(os.environ)
    full_env.update(env or {})
    result = subprocess.run(
        [str(cwd / ("cbuild" + EXE)), *args], cwd=cwd, env=full_env, input=STDIN_FOR_BUILD,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        encoding="utf-8", errors="replace", timeout=HANG_LIMIT_SECONDS)
    return result.returncode, result.stdout


def cbuild(*args, env=None, cwd=WORK):
    """Run the build program in cwd; returns (exit status, combined output without colors)."""
    status, log = cbuild_raw(*args, env=env, cwd=cwd)
    return status, ANSI_ESCAPE.sub("", log)


def run_tests(*args, env=None):
    status, log = cbuild("--test", *args, env=env)
    check(status == 0, f"--test {args} failed with status {status}:\n{log}")
    return log


def run_failing_tests(*args, env=None):
    status, log = cbuild("--test", *args, env=env)
    check(status == 1, f"--test {args} exited with status {status}, expected 1:\n{log}")
    return log


def result_line(log, name, result):
    """The runner's line for one test, or None."""
    pattern = re.compile(rf"^{result}\s+\[\d+/\d+\] {re.escape(name)} \(", re.MULTILINE)
    match = pattern.search(log)
    if not match:
        return None
    return log[match.start():log.index("\n", match.start())]


def expect_result(log, name, result):
    line = result_line(log, name, result)
    check(line, f"expected {result} for {name}:\n{log}")
    return line


def expect_summary(log, passed, failed, skipped):
    expected = f"Tests: {passed} passed, {failed} failed, {skipped} skipped"
    check(expected in log, f"expected '{expected}':\n{log}")


def ran_tests(log):
    return sorted(re.findall(r"^(?:PASS|FAIL|SKIP)\s+\[\d+/\d+\] (.+?) \(", log, re.MULTILINE))


def listed_tests(*args):
    status, log = cbuild("--list-tests", *args)
    check(status == 0, f"--list-tests {args} failed with status {status}:\n{log}")
    names = []
    for line in log.splitlines():
        match = re.match(r"^  (\S+)(?:\s+\[.*\])?$", line)
        if match and match.group(1) != "(no":
            names.append(match.group(1))
    return sorted(names), log


def read_log(name):
    return (WORK / "build" / "tests" / f"{name}.log").read_text(encoding="utf-8")


def compile_build_program(cwd=WORK):
    if WINDOWS:
        command = [CC, "/nologo", "/std:c11", "/W4", "/D_CRT_SECURE_NO_WARNINGS",
                   f"/I{ROOT}", "build.c", "/Fe:cbuild.exe"]
    else:
        flags = os.environ.get("CFLAGS") or "-std=c11 -Wall -Wextra -Wpedantic -Werror"
        command = [*shlex.split(CC), *shlex.split(flags), f"-I{ROOT}",
                   "build.c", "-o", "cbuild"]
    subprocess.run(command, cwd=cwd, check=True)


def listing_builds_nothing():
    names, _ = listed_tests("--test-filter=sel_*")
    check("sel_plain" in names, f"selection tests are missing: {names}")
    check(not (WORK / "build").exists(), "--list-tests built something")


def listing_shows_every_label():
    _, log = listed_tests("--test-filter=sel_alpha_one")
    check(re.search(r"^  sel_alpha_one\s+\[sel_a,sel_c\]$", log, re.MULTILINE),
          f"labels are missing:\n{log}")


def listing_reports_an_empty_selection():
    names, log = listed_tests("--test-label=no-such-label")
    check(names == [] and "(no tests)" in log, f"expected no tests:\n{log}")


def list_tests_takes_precedence_over_test():
    status, log = cbuild("--test", "--list-tests", "--test-filter=res_pass")
    check(status == 0 and ran_tests(log) == [], f"tests ran while listing:\n{log}")


def selection_combines_labels_filters_and_excludes():
    cases = [
        (["--test-label=sel_a"], ["sel_alpha_one"]),
        (["--test-label=sel_c"], ["sel_alpha_one"]),
        (["--test-label=sel_a", "--test-label=sel_b"],
         ["sel_alpha_one", "sel_alpha_two", "sel_beta_one"]),
        (["--test-filter=sel_alpha_*"], ["sel_alpha_one", "sel_alpha_two"]),
        (["--test-filter=sel_alpha_*", "--test-filter=sel_beta_one"],
         ["sel_alpha_one", "sel_alpha_two", "sel_beta_one"]),
        (["--test-filter=sel_?lpha_one"], ["sel_alpha_one"]),
        (["--test-filter=sel_*", "--test-exclude=*_one"], ["sel_alpha_two", "sel_plain"]),
        (["--test-filter=sel_*", "--test-exclude=*_one", "--test-exclude=*alpha*"], ["sel_plain"]),
        (["--test-label=sel_b", "--test-filter=*_one"], ["sel_beta_one"]),
        (["--test-label=sel_b", "--test-exclude=*beta*"], ["sel_alpha_two"]),
        (["--test-label=sel_a", "--test-filter=*beta*"], []),
    ]
    for flags, expected in cases:
        names, log = listed_tests(*flags)
        check(names == expected, f"{flags} listed {names}, expected {expected}:\n{log}")


def running_matches_the_selection():
    log = run_tests("--test-label=sel_b", "--test-exclude=*beta*")
    check(ran_tests(log) == ["sel_alpha_two"], f"wrong tests ran:\n{log}")
    expect_summary(log, 1, 0, 0)


def an_empty_selection_succeeds_without_building():
    shutil.rmtree(WORK / "build", ignore_errors=True)
    log = run_tests("--test-label=no-such-label")
    check("no tests selected" in log, f"no warning:\n{log}")
    check(not (WORK / "build").exists(), "an empty selection built something")


def selection_flags_need_values():
    for flag in ("--test-label", "--test-filter", "--test-exclude"):
        status, log = cbuild("--test", flag)
        check(status != 0 and "requires a value" in log, f"{flag} without a value:\n{log}")
        status, log = cbuild("--test", f"{flag}=")
        check(status != 0 and "requires a value" in log, f"{flag}= without a value:\n{log}")
        check(ran_tests(log) == [], f"tests ran after a bad {flag}:\n{log}")


def unexpected_arguments_stop_the_run():
    status, log = cbuild("--test", "--test-filter=res_pass", "stray")
    check(status != 0 and "unexpected argument" in log, f"stray argument accepted:\n{log}")
    check(ran_tests(log) == [], f"tests ran after a bad argument:\n{log}")


def only_failures_print_their_output():
    log = run_tests("--test-label=res_pass")
    expect_result(log, "res_pass", "PASS")
    expect_summary(log, 1, 0, 0)
    check("passing-output" not in log, f"a passing test printed its output:\n{log}")

    log = run_failing_tests("--test-label=res_fail")
    line = expect_result(log, "res_fail", "FAIL")
    check("exit code 3" in line, f"exit code is missing: {line}")
    check("failing-stdout" in log, f"failure stdout was not printed:\n{log}")
    check(re.search(r"^failing-stderr-no-newline$", log, re.MULTILINE),
          f"failure output was not terminated with a newline:\n{log}")
    check(re.search(r"^Failed tests \(logs in build/tests\):\n  res_fail$", log, re.MULTILINE),
          f"failure summary is missing:\n{log}")
    expect_summary(log, 0, 1, 0)


def skips_do_not_fail_the_run():
    log = run_tests("--test-label=res_skip")
    expect_result(log, "res_skip", "SKIP")
    expect_summary(log, 0, 0, 1)
    check("skipped-output" not in log, f"a skipped test printed its output:\n{log}")
    check("skipped-output" in read_log("res_skip"), "a skipped test left no log")


def other_nonzero_exits_fail():
    log = run_failing_tests("--test-label=res_fail_more")
    expect_summary(log, 0, 4, 0)
    check("exit code 125" in expect_result(log, "res_skip_code_unset", "FAIL"),
          f"a skip code without set_skip_code must fail:\n{log}")
    check("exit code 3" in expect_result(log, "res_skip_code_other", "FAIL"),
          f"an exit status other than the skip code must fail:\n{log}")
    terminated = expect_result(log, "res_terminated", "FAIL")
    if not WINDOWS:
        check("exit code 143" in terminated, f"signal death must report 128+N: {terminated}")
    check("could not start no-such-program-for-cbuild-tests"
          in expect_result(log, "res_cannot_start", "FAIL"),
          f"a missing program must be reported:\n{log}")


def logs_capture_stdout_and_stderr():
    run_failing_tests("--test-label=res_fail")
    text = read_log("res_fail")
    check("failing-stdout" in text and "failing-stderr-no-newline" in text,
          f"log is incomplete: {text!r}")


def logs_are_named_after_sanitized_test_names():
    log = run_tests("--test-label=env_name")
    expect_result(log, "weird name/with:chars", "PASS")
    check("weird-output" in read_log("weird_name_with_chars"), "sanitized log is incomplete")


def logs_follow_the_config_output_dir():
    (WORK / "build" / "tests" / "res_pass.log").unlink(missing_ok=True)
    run_tests("--test-label=res_pass", env={"RUNNER_CONFIG_OUTDIR": "alt out"})
    check((WORK / "alt out" / "tests" / "res_pass.log").is_file(),
          "log did not follow the config output directory")
    check(not (WORK / "build" / "tests" / "res_pass.log").exists(),
          "log was written to the context output directory")
    shutil.rmtree(WORK / "alt out")


def reruns_replace_logs_and_tests_inherit_the_environment():
    run_tests("--test-label=env_var", env={"RUNNER_VALUE": "a-long-first-value"})
    check("env=a-long-first-value" in read_log("env_var"), "environment was not inherited")
    run_tests("--test-label=env_var", env={"RUNNER_VALUE": "short"})
    text = read_log("env_var")
    check("env=short" in text and "first-value" not in text, f"log was not replaced: {text!r}")


def tests_never_see_the_build_programs_stdin():
    expect_result(run_tests("--test-label=env_stdin"), "env_stdin", "PASS")


def arguments_arrive_intact():
    run_tests("--test-label=env_args")
    expected = ["two words", "", 'quote"d', "back\\slash", "trailing space\\", "a*b"]
    actual = [line[1:-1] for line in read_log("env_args").splitlines()]
    check(actual == expected, f"arguments arrived as {actual}, expected {expected}")


def programs_can_live_at_paths_with_spaces():
    run_tests("--test-filter=env_spaced_program")
    check("spaced-output" in read_log("env_spaced_program"), "spaced program did not run")


def own_timeouts_kill_the_test():
    started = time.monotonic()
    log = run_failing_tests("--test-label=time_own")
    check(time.monotonic() - started < HANG_LIMIT_SECONDS / 2, "timed-out test was not killed")
    check("timed out after 1s" in expect_result(log, "time_own", "FAIL"), f"no timeout:\n{log}")
    check("before-timeout" in log, f"output before the timeout was not printed:\n{log}")
    check("before-timeout" in read_log("time_own"), "output before the timeout was not logged")


def the_default_timeout_covers_tests_without_their_own():
    log = run_failing_tests("--test-label=time_default", "--test-timeout=1", "-j2")
    expect_summary(log, 0, 2, 0)
    for name in ("time_default", "time_negative"):
        check("timed out after 1s" in expect_result(log, name, "FAIL"), f"no timeout:\n{log}")


def a_tests_own_timeout_beats_the_default():
    expect_result(run_tests("--test-label=time_longer", "--test-timeout=1"), "time_own_longer", "PASS")


def tests_run_in_parallel():
    log = run_tests("--test-label=par", "-j2")
    check("Test: 2 tests, 2 jobs" in log, f"wrong job count:\n{log}")


def one_job_runs_tests_one_at_a_time():
    log = run_tests("--test-label=serial", "-j1")
    check("Test: 3 tests, 1 job " in log, f"wrong job count:\n{log}")
    expect_summary(log, 3, 0, 0)


def locked_tests_never_overlap():
    expect_summary(run_tests("--test-label=lock_same", "-j4"), 4, 0, 0)


def different_locks_do_not_block_each_other():
    expect_summary(run_tests("--test-label=lock_distinct", "-j2"), 2, 0, 0)


def cleared_locks_do_not_block():
    expect_summary(run_tests("--test-label=lock_cleared", "-j2"), 2, 0, 0)


def a_blocked_test_does_not_hold_up_runnable_ones():
    expect_summary(run_tests("--test-label=lock_pass_over", "-j2"), 3, 0, 0)


def unselected_dependencies_are_not_built():
    run_tests("--test-label=res_pass")
    check(not (WORK / "dep-marker.txt").exists(), "an unselected dependency was built")
    check(not (WORK / "build" / ("versioned" + EXE)).exists(), "an unselected target was built")


def added_dependencies_build_before_the_test():
    expect_result(run_tests("--test-label=dep_extra"), "dep_extra", "PASS")


def non_executable_targets_build_before_args0_runs():
    expect_result(run_tests("--test-filter=dep_program_with_target"), "dep_program_with_target", "PASS")


def up_to_date_tests_rerun_without_compiling():
    run_tests("--test-label=res_pass")
    log = run_tests("--test-label=res_pass")
    expect_result(log, "res_pass", "PASS")
    check(not re.search(r"^(COMPILE|LINK)\b", log, re.MULTILINE), f"up-to-date test rebuilt:\n{log}")


def source_edits_rebuild_before_the_test_runs():
    source = WORK / "src" / "versioned.c"
    original = source.read_text(encoding="utf-8")
    run_tests("--test-filter=dep_versioned")
    time.sleep(0.1)
    source.write_text(original.replace("return 0", "return 7"), encoding="utf-8", newline="\n")
    log = run_failing_tests("--test-filter=dep_versioned")
    check("exit code 7" in expect_result(log, "dep_versioned", "FAIL"), f"stale program ran:\n{log}")
    time.sleep(0.1)
    source.write_text(original, encoding="utf-8", newline="\n")
    expect_result(run_tests("--test-filter=dep_versioned"), "dep_versioned", "PASS")


def build_failures_stop_the_run():
    for flag in ("--test-filter=broken_target", "--test-filter=broken_dep"):
        log = run_failing_tests(flag)
        check("Build failed; tests not run." in log, f"no build failure reported:\n{log}")
        check(ran_tests(log) == [], f"tests ran after a failed build:\n{log}")
        check("Test:" not in log, f"runner started after a failed build:\n{log}")


def sections_separate_the_build_from_the_run():
    log = run_tests("--test-label=res_pass")
    check(re.search(r"^── Build ─+\n(?:.*\n)*?✔ Up to date\.\n\n── Test: 1 test, 1 job ─+$", log, re.MULTILINE),
          f"build and test sections are not separated:\n{log}")


def nested_builds_report_only_their_steps_and_errors():
    nested = {"CBUILD_NESTED": "sub"}
    shutil.rmtree(WORK / "build", ignore_errors=True)
    status, log = cbuild("--target=versioned", env=nested)
    check(status == 0, f"nested build failed:\n{log}")
    check("──" not in log and "Build succeeded" not in log, f"nested build was not quiet:\n{log}")
    check(log.count("build subproject sub") == 1, f"nested build did not name itself once:\n{log}")

    status, log = cbuild("--target=versioned", env=nested)
    check(status == 0 and log == "", f"up-to-date nested build printed output:\n{log}")

    status, log = cbuild("--target=broken", env=nested)
    check(status != 0 and "Compilation failed" in log, f"nested build hid its error:\n{log}")
    check("✖" not in log, f"nested build printed a final failure:\n{log}")


def plain_builds_and_cleans_have_sections():
    status, log = cbuild("--target=versioned")
    check(status == 0 and re.search(r"^── Build ─+$", log, re.MULTILINE), f"no build section:\n{log}")
    status, log = cbuild("--target=versioned")
    check(status == 0 and "✔ Up to date." in log, f"no up-to-date status:\n{log}")
    status, log = cbuild("--clean")
    check(status == 0 and re.search(r"^── Clean ─+$", log, re.MULTILINE), f"no clean section:\n{log}")


def color_follows_the_environment():
    plain = {"NO_COLOR": "", "CLICOLOR_FORCE": ""}
    _, log = cbuild_raw("--test", "--test-label=res_pass", env=plain)
    check("\x1b" not in log, f"piped output has color:\n{log!r}")

    forced = {"NO_COLOR": "", "CLICOLOR_FORCE": "1"}
    for label, expected in (("res_pass", "\x1b[32mPASS"), ("res_skip", "\x1b[33mSKIP"), ("res_fail", "\x1b[31mFAIL")):
        _, log = cbuild_raw("--test", f"--test-label={label}", env=forced)
        check(expected in log, f"expected {expected!r} for {label}:\n{log!r}")

    _, log = cbuild_raw("--test", "--test-label=res_pass", env={"NO_COLOR": "1", "CLICOLOR_FORCE": "1"})
    check("\x1b" not in log, f"NO_COLOR did not win:\n{log!r}")


def copy_example_project():
    """Copy the repository example and compile its build program; returns its directory."""
    parent = WORK / "example-copy"
    project = parent / "example"
    parent.mkdir()
    shutil.copy2(ROOT / "cbuild.h", parent / "cbuild.h")
    shutil.copytree(ROOT / "example", project, ignore=shutil.ignore_patterns(
        "build", "cbuild", "cbuild.exe", "*.old", "*.obj", "*.log", "compile_commands.json"))
    compile_build_program(project)
    return project


def expect_summary_matches_steps(log):
    compiled = len(re.findall(r"^COMPILE\b", log, re.MULTILINE))
    linked = len(re.findall(r"^LINK\b", log, re.MULTILINE))
    commands = len(re.findall(r"^COMMAND\b", log, re.MULTILINE)) - log.count("build subproject")
    commands -= log.count("bootstrap math subproject")
    parts = [f"{count} {name}" for count, name in ((compiled, "compiled"), (linked, "linked")) if count]
    if commands:
        parts.append(f"{commands} command{'' if commands == 1 else 's'}")
    expected = f"Build succeeded ({', '.join(parts)}, "
    check(expected in log, f"expected '{expected}':\n{log}")


SETUP_CHILD_BUILD = """
#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    if (cbuild_run_command(ctx, cbuild_command(ctx, "prepare", "echo prepared")) != 0) return 1;
    cbuild_add_source(ctx, cbuild_static_library(ctx, "Setup"), "setup.c");
    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
"""

SETUP_PARENT_BUILD = """
#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#ifdef _WIN32
#define CHILD_EXE ".\\\\cbuild.exe"
#else
#define CHILD_EXE "./cbuild"
#endif

int main(int argc, char **argv) {
    cbuild_context_t *ctx = cbuild_context_new();
    subproject_t *child = cbuild_add_subproject(ctx, "child", "child", CHILD_EXE);
    target_t *setup = cbuild_subproject_get_target(ctx, child, "Setup");
    if (!setup) return 1;
    target_t *app = cbuild_executable(ctx, "app");
    cbuild_add_source(ctx, app, "main.c");
    cbuild_add_link_target(ctx, app, setup);
    int result = cbuild_run(ctx, argc, argv);
    cbuild_context_free(ctx);
    return result;
}
"""


def setup_output_does_not_confuse_the_manifest():
    parent = WORK / "setup-manifest"
    child = parent / "child"
    child.mkdir(parents=True)
    (child / "build.c").write_text(SETUP_CHILD_BUILD, encoding="utf-8")
    (child / "setup.c").write_text("int setup_value(void) { return 7; }\n", encoding="utf-8")
    (parent / "build.c").write_text(SETUP_PARENT_BUILD, encoding="utf-8")
    (parent / "main.c").write_text(
        "int setup_value(void);\nint main(void) { return setup_value() == 7 ? 0 : 1; }\n", encoding="utf-8")
    compile_build_program(child)
    compile_build_program(parent)

    _, manifest = cbuild("--manifest", cwd=child)
    check("── Setup" in manifest, f"child did not print its setup section:\n{manifest}")
    check("static_lib Setup build/" in manifest, f"manifest path does not use the default directory:\n{manifest}")
    for flag in ("--list", "--graph", "--deps=Setup"):
        _, log = cbuild(flag, cwd=child)
        check("(null)" not in log, f"{flag} printed a null path:\n{log}")
    status, log = cbuild(cwd=parent)
    check(status == 0 and "Build succeeded" in log, f"subproject target did not build and link:\n{log}")


def listings_show_the_paths_builds_use():
    late = {"RUNNER_LATE_OUTDIR": "late"}
    for flag in ("--list", "--manifest"):
        status, log = cbuild(flag, env=late)
        check(status == 0 and f"late/probe{EXE}" in log, f"{flag} ignored the output directory:\n{log}")
        check(f"build/probe{EXE}" not in log, f"{flag} printed an outdated path:\n{log}")
    status, log = cbuild("--target=versioned", env=late)
    check(status == 0 and (WORK / "late" / ("versioned" + EXE)).is_file(), f"build ignored the output directory:\n{log}")
    shutil.rmtree(WORK / "late")


def subprojects_report_through_their_parent():
    project = copy_example_project()
    source = project / "lib" / "src" / "math.c"
    original = source.read_text(encoding="utf-8")

    status, log = cbuild(cwd=project)
    check(status == 0, f"example build failed:\n{log}")
    expect_summary_matches_steps(log)
    check(re.search(r"\A── Setup ─+\nCOMMAND\s+bootstrap math subproject\n(?:.*\n)*?\n── Build ─+$", log, re.MULTILINE),
          f"bootstrap is not in a setup section:\n{log}")
    check(log.count("Build succeeded") == 1 and log.count("── Build") == 1, f"subproject was not nested:\n{log}")
    check(log.count("build subproject math_project") == 1, f"subproject was not named once:\n{log}")
    check(re.search(r"^COMPILE\s+\[2/2\] src/", log, re.MULTILINE), f"no compile progress:\n{log}")

    status, log = cbuild(cwd=project)
    check(status == 0 and "✔ Up to date." in log, f"no-op build did not say so:\n{log}")
    check("COMMAND" not in log, f"no-op build showed the subproject:\n{log}")
    check("Setup" not in log, f"no-op build had a setup section:\n{log}")

    time.sleep(0.1)
    source.write_text(original + "\n", encoding="utf-8", newline="\n")
    status, log = cbuild(cwd=project)
    check(status == 0 and "build subproject math_project" in log, f"edited subproject did not rebuild:\n{log}")
    expect_summary_matches_steps(log)

    time.sleep(0.1)
    source.write_text(original + "\nnot valid c\n", encoding="utf-8", newline="\n")
    status, log = cbuild(cwd=project)
    check(status != 0 and "Compilation failed" in log, f"broken subproject built:\n{log}")
    check(log.count("✖") == 1 and "✖ Build failed." in log, f"expected one final failure line:\n{log}")

    time.sleep(0.1)
    source.write_text(original, encoding="utf-8", newline="\n")
    status, log = cbuild("--clean", cwd=project)
    check(status == 0 and log.count("Clean complete") == 1, f"subproject clean was not nested:\n{log}")
    check(log.count("Cleaning build outputs") == 1, f"subproject clean printed its steps:\n{log}")


def unselected_broken_targets_do_not_matter():
    log = run_tests("--test-filter=res_pass")
    check("broken.c" not in log, f"an unselected broken target was compiled:\n{log}")


def flag_phases_wrap_the_run():
    log = run_tests("--test-label=res_pass", "--before-check", "--after-check")
    positions = [log.find(marker) for marker in
                 ("before-flag-ran", "Test: 1 test", "Tests: 1 passed", "after-flag-ran")]
    check(-1 not in positions and positions == sorted(positions), f"wrong phase order:\n{log}")

    log = run_failing_tests("--test-label=res_fail", "--before-check", "--after-check")
    check("before-flag-ran" in log, f"before-build flag did not run:\n{log}")
    check("after-flag-ran" not in log, f"after-build flag ran after a failure:\n{log}")


SCENARIOS = [
    listing_builds_nothing,
    listing_shows_every_label,
    listing_reports_an_empty_selection,
    list_tests_takes_precedence_over_test,
    selection_combines_labels_filters_and_excludes,
    running_matches_the_selection,
    an_empty_selection_succeeds_without_building,
    selection_flags_need_values,
    unexpected_arguments_stop_the_run,
    only_failures_print_their_output,
    skips_do_not_fail_the_run,
    other_nonzero_exits_fail,
    logs_capture_stdout_and_stderr,
    logs_are_named_after_sanitized_test_names,
    logs_follow_the_config_output_dir,
    reruns_replace_logs_and_tests_inherit_the_environment,
    tests_never_see_the_build_programs_stdin,
    arguments_arrive_intact,
    programs_can_live_at_paths_with_spaces,
    own_timeouts_kill_the_test,
    the_default_timeout_covers_tests_without_their_own,
    a_tests_own_timeout_beats_the_default,
    tests_run_in_parallel,
    one_job_runs_tests_one_at_a_time,
    locked_tests_never_overlap,
    different_locks_do_not_block_each_other,
    cleared_locks_do_not_block,
    a_blocked_test_does_not_hold_up_runnable_ones,
    unselected_dependencies_are_not_built,
    added_dependencies_build_before_the_test,
    non_executable_targets_build_before_args0_runs,
    up_to_date_tests_rerun_without_compiling,
    source_edits_rebuild_before_the_test_runs,
    build_failures_stop_the_run,
    sections_separate_the_build_from_the_run,
    unselected_broken_targets_do_not_matter,
    flag_phases_wrap_the_run,
    color_follows_the_environment,
    nested_builds_report_only_their_steps_and_errors,
    plain_builds_and_cleans_have_sections,
    subprojects_report_through_their_parent,
    setup_output_does_not_confuse_the_manifest,
    listings_show_the_paths_builds_use,
]


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    failed = 0
    try:
        shutil.copytree(ROOT / "tests" / "runner", WORK)
        compile_build_program()
        for scenario in SCENARIOS:
            name = scenario.__name__.replace("_", " ")
            try:
                scenario()
            except (Failure, OSError, subprocess.TimeoutExpired) as failure:
                print(f"test-runner: FAILED: {name}\n{failure}", file=sys.stderr)
                failed += 1
                continue
            print(f"test-runner: ok: {name}", flush=True)
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    if failed:
        print(f"test-runner: {failed} of {len(SCENARIOS)} scenarios failed", file=sys.stderr)
        return 1
    print("test-runner: all scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
