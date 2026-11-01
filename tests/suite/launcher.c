#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define PROGRAM "cbuild" EXE
#define COMPILING "compiling build.c"

#ifdef __FreeBSD__
#define INSTALL_DIR "bin"
#else
#define INSTALL_DIR ".local/bin"
#endif
#define INSTALLED INSTALL_DIR "/cbuild" EXE

#ifdef _WIN32
#define PATH_LIST_SEPARATOR ";"
#define PATH_NOTE "Added to your user PATH"
#else
#define PATH_LIST_SEPARATOR ":"
#define PATH_NOTE "is not on PATH"
#endif

static const char *const echo_program =
    "/* CBUILD_IMPLEMENTATION */\n"
    "#include <stdio.h>\n"
    "int main(int argc, char **argv) {\n"
    "    for (int i = 1; i < argc; ++i) printf(\"[%s]\\n\", argv[i]);\n"
    "    return 7;\n"
    "}\n";

static const char *printing_program(const char *text) {
    return format_text("/* CBUILD_IMPLEMENTATION */\n"
                       "#include <stdio.h>\n"
                       "int main(void) {\n"
                       "    puts(\"%s\");\n"
                       "    return 0;\n"
                       "}\n",
                       text);
}

static const char *fake_compiler_command(const char *name, const char *mode) {
    return format_text("\"%s\" --fake-compiler %s %s", h_self(), name, mode);
}

static const char *scratch_home(void) {
    const char *home = format_text("%s/home", current_dir());
    make_dir(home);
    set_env("HOME", home);
    return home;
}

static const char *directory_outside_any_project(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) base = "/tmp";
    const char *directory = format_text("%s/cbuild-launcher-test", base);
    delete_tree(directory);
    make_dirs(directory);
    return directory;
}

#ifdef _WIN32
static const char *environment_key;

static void delete_environment_key(void) {
    char command[1024];
    snprintf(command, sizeof(command), "reg delete \"HKCU\\%s\" /f >NUL 2>&1", environment_key);
    system(command);
}

static const char *environment_key_path(void) {
    return format_text("HKCU\\%s", environment_key);
}

static void seed_user_path(const char *type, const char *value) {
    outcome_t outcome = RUN("reg", "add", environment_key_path(), "/v", "Path", "/t", type, "/d", value, "/f");
    CHECK(outcome.status == 0, "could not seed the user PATH");
}

static const char *user_path_value(void) {
    outcome_t outcome = RUN("reg", "query", environment_key_path(), "/v", "Path");
    CHECK(outcome.status == 0, "could not read the user PATH");
    return outcome.log;
}

static const char *installed_directory(void) {
    return replace_text(format_text("%s/%s", scratch_home(), INSTALL_DIR), "/", "\\");
}
#endif

void launcher_setup(void) {
    copy_tree(root_path("tests/fixtures/project"), ".");
    copy_file(root_path("cbuild.h"), "cbuild.h");
    use_program(prebuilt_program("launcher"));
#ifdef _WIN32
    const char *scratch = current_dir();
    const char *name = strrchr(scratch, '\\');
    environment_key = format_text("Software\\cbuild-launcher-test-%s", name ? name + 1 : scratch);
    set_env("CBUILD_LAUNCHER_ENV_KEY", environment_key);
    atexit(delete_environment_key);
#endif
}

SCENARIO(launcher, first_run_compiles_the_build_program) {
    EXPECT_GONE(PROGRAM);
    const char *log = BUILD("--list");
    EXPECT_HAS(log, COMPILING);
    EXPECT_LINE(log, "  test_app *[executable]*");
    EXPECT_FILE(PROGRAM);
    EXPECT_LACKS(BUILD("--list"), COMPILING);
}

SCENARIO(launcher, project_is_found_from_a_subdirectory) {
    change_dir("src");
    BUILD("--target=test_app");
    EXPECT_FILE("../" PROGRAM);
    EXPECT_FILE("../build/test_app" EXE);
    EXPECT_GONE(PROGRAM);
    EXPECT_GONE("build");
}

SCENARIO(launcher, files_without_the_implementation_are_skipped) {
    write_text("src/build.c", "int unrelated;\n");
    change_dir("src");
    EXPECT_LINE(BUILD("--list"), "  test_app *[executable]*");
    EXPECT_FILE("../" PROGRAM);
    EXPECT_GONE(PROGRAM);
}

SCENARIO(launcher, the_nearest_build_program_wins) {
    make_dirs("nested/deeper");
    write_text("nested/build.c", printing_program("nested program"));
    change_dir("nested/deeper");
    EXPECT_HAS(BUILD(), "nested program");
    EXPECT_FILE("../" PROGRAM);
    EXPECT_GONE("../../" PROGRAM);
}

SCENARIO(launcher, arguments_and_exit_status_pass_through) {
    write_text("build.c", echo_program);
    outcome_t outcome = CBUILD("one two", "--three=four five", "--help", "--version", "");
    CHECK(outcome.status == 7, "the exit status was %d", outcome.status);
    EXPECT_ORDER(outcome.log, "[one two]\n", "[--three=four five]\n", "[--help]\n", "[--version]\n", "[]\n");
}

SCENARIO(launcher, build_program_still_rebuilds_itself) {
    BUILD("--list");
    settle();
    touch_file("build.c");
    change_dir("src");
    const char *log = BUILD("--list");
    EXPECT_HAS(log, "Detected changes");
    EXPECT_LACKS(log, COMPILING);
    EXPECT_LACKS(BUILD("--list"), "Detected changes");
}

SCENARIO(launcher, staleness_is_left_to_the_build_program) {
    write_text("build.c", printing_program("first version"));
    EXPECT_HAS(BUILD(), "first version");
    settle();
    write_text("build.c", printing_program("second version"));
    const char *log = BUILD();
    EXPECT_HAS(log, "first version");
    EXPECT_LACKS(log, COMPILING);
}

SCENARIO(launcher, compiler_choice_matches_self_rebuild) {
    set_env("CC_FOR_BUILD", fake_compiler_command("host", "fail"));
    set_env("CC", fake_compiler_command("target", "fail"));
    BUILD_FAILS("--list");
    EXPECT_ORDER(read_text("compilers-used.txt"), "host\n");
    EXPECT_LACKS(read_text("compilers-used.txt"), "target");

    unset_env("CC_FOR_BUILD");
    BUILD_FAILS("--list");
    EXPECT_ORDER(read_text("compilers-used.txt"), "host\ntarget\n");
}

SCENARIO(launcher, failed_compile_stops_the_run) {
    write_text("build.c", "/* CBUILD_IMPLEMENTATION */\nnot valid c\n");
    const char *log = BUILD_FAILS("--list");
    EXPECT_HAS(log, "could not compile");
    EXPECT_GONE(PROGRAM);
}

SCENARIO(launcher, a_directory_in_the_way_is_reported) {
    make_dir(PROGRAM);
    EXPECT_HAS(BUILD_FAILS(), "is a directory");
}

SCENARIO(launcher, unstartable_program_is_reported) {
    write_text(PROGRAM, "this is not a program\n");
    EXPECT_HAS(BUILD_FAILS("--list"), "could not start");
}

SCENARIO(launcher, outside_a_project_it_answers_for_itself) {
    const char *back = current_dir();
    const char *outside = directory_outside_any_project();
    change_dir(outside);
    EXPECT_HAS(BUILD_FAILS(), "no build.c");
    EXPECT_HAS(BUILD_FAILS("--list"), "no build.c");
    EXPECT_HAS(BUILD("--help"), "--launcher-install");
    EXPECT_LINE(BUILD("--version"), CBUILD_VERSION);
    change_dir(back);
    delete_tree(outside);
}

SCENARIO(launcher, own_flags_skip_the_project) {
    EXPECT_LINE(BUILD("--launcher-version"), CBUILD_VERSION);
    EXPECT_HAS(BUILD("--launcher-help"), "--launcher-install");
    EXPECT_GONE(PROGRAM);
}

SCENARIO(launcher, install_copies_a_working_launcher) {
    const char *installed = format_text("%s/%s", scratch_home(), INSTALLED);
    EXPECT_HAS(BUILD("--launcher-install"), "Installed");
    EXPECT_FILE(installed);
    EXPECT_GONE(PROGRAM);

    use_program(installed);
    EXPECT_LINE(BUILD("--list"), "  test_app *[executable]*");
    EXPECT_FILE(PROGRAM);
}

SCENARIO(launcher, install_replaces_an_existing_copy) {
    const char *home = scratch_home();
    const char *installed = format_text("%s/%s", home, INSTALLED);
    make_dirs(format_text("%s/%s", home, INSTALL_DIR));
    write_text(installed, "an older launcher\n");
    BUILD("--launcher-install");

    use_program(installed);
    EXPECT_LINE(BUILD("--launcher-version"), CBUILD_VERSION);
    EXPECT_GONE(format_text("%s.tmp", installed));
    EXPECT_GONE(format_text("%s.old", installed));
}

SCENARIO(launcher, install_reports_whether_path_has_it) {
    const char *bin = format_text("%s/%s", scratch_home(), INSTALL_DIR);
    EXPECT_HAS(BUILD("--launcher-install"), PATH_NOTE);
    set_env("PATH", format_text("%s/%s%s", bin, PATH_LIST_SEPARATOR, getenv("PATH")));
    EXPECT_LACKS(BUILD("--launcher-install"), PATH_NOTE);
}

SCENARIO(launcher, install_needs_a_home_directory) {
    unset_env("HOME");
    unset_env("USERPROFILE");
    EXPECT_HAS(BUILD_FAILS("--launcher-install"), "HOME is not set");
}

SCENARIO(launcher, install_appends_to_the_user_path_once) {
#ifndef _WIN32
    h_skip("only Windows keeps the user PATH in the registry");
#else
    const char *bin = installed_directory();
    seed_user_path("REG_EXPAND_SZ", "%SystemRoot%\\keep;C:\\other");
    EXPECT_HAS(BUILD("--launcher-install"), "Added to your user PATH");
    const char *value = user_path_value();
    EXPECT_HAS(value, "REG_EXPAND_SZ");
    EXPECT_HAS(value, format_text("%%SystemRoot%%\\keep;C:\\other;%s", bin));

    EXPECT_HAS(BUILD("--launcher-install"), "Already on your user PATH");
    CHECK(count_text(user_path_value(), bin) == 1, "the directory was added twice");
#endif
}

SCENARIO(launcher, install_keeps_a_plain_string_user_path) {
#ifndef _WIN32
    h_skip("only Windows keeps the user PATH in the registry");
#else
    const char *bin = installed_directory();
    seed_user_path("REG_SZ", "C:\\other;");
    BUILD("--launcher-install");
    const char *value = user_path_value();
    EXPECT_HAS(value, "REG_SZ");
    EXPECT_HAS(value, format_text("C:\\other;%s", bin));
#endif
}

SCENARIO(launcher, install_creates_a_missing_user_path) {
#ifndef _WIN32
    h_skip("only Windows keeps the user PATH in the registry");
#else
    const char *bin = installed_directory();
    BUILD("--launcher-install");
    const char *value = user_path_value();
    EXPECT_HAS(value, "REG_EXPAND_SZ");
    EXPECT_HAS(value, bin);
#endif
}

SCENARIO(launcher, install_recognizes_an_unexpanded_entry) {
#ifndef _WIN32
    h_skip("only Windows keeps the user PATH in the registry");
#else
    installed_directory();
    seed_user_path("REG_EXPAND_SZ", "C:\\other;%HOME%\\.local\\bin\\");
    EXPECT_HAS(BUILD("--launcher-install"), "Already on your user PATH");
    const char *value = user_path_value();
    EXPECT_HAS(value, "C:\\other;%HOME%\\.local\\bin\\");
    CHECK(count_text(value, ".local") == 1, "the directory was added again");
#endif
}

SCENARIO(launcher, install_leaves_an_unusable_user_path_alone) {
#ifndef _WIN32
    h_skip("only Windows keeps the user PATH in the registry");
#else
    installed_directory();
    seed_user_path("REG_DWORD", "5");
    EXPECT_HAS(BUILD("--launcher-install"), "is not on PATH");
    const char *value = user_path_value();
    EXPECT_HAS(value, "REG_DWORD");
    EXPECT_LACKS(value, ".local");
#endif
}
