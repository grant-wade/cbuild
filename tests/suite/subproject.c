#include "harness.h"

void subproject_setup(void) {
    copy_tree(root_path("tests/fixtures/subproject"), ".");
    copy_file(root_path("cbuild.h"), "cbuild.h");
    copy_file(root_path("cbuild.h"), "child/cbuild.h");
    change_dir("child");
    compile_build_program();
    change_dir("..");
}

SCENARIO(subproject, setup_output_does_not_confuse_the_manifest) {
    change_dir("child");
    const char *manifest = BUILD("--manifest");
    EXPECT_LINE(manifest, SECTION_BAR SECTION_BAR " Setup *");
    EXPECT_HAS(manifest, "static_lib Setup build/");
    EXPECT_LACKS(BUILD("--list"), "(null)");
    EXPECT_LACKS(BUILD("--graph"), "(null)");
    EXPECT_LACKS(BUILD("--deps=Setup"), "(null)");
}

SCENARIO(subproject, parent_builds_and_links_child_targets) {
    compile_build_program();
    EXPECT_HAS(BUILD(), "Build succeeded");
    outcome_t outcome = RUN("build/app" EXE);
    CHECK(outcome.status == 0, "the linked app returned %d", outcome.status);
}
