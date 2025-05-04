#define CBUILD_IMPLEMENTATION
#include "cbuild.h"
#include <stdbool.h>

int main(int argc, char** argv) {
    CBUILD_SELF_REBUILD("build.c", "cbuild.h");
    cbuild_set_output_dir("build");
    cbuild_enable_compile_commands(true);

    CBUILD_SUBPROJECT(math, "lib", "./cbuild");
    target_t* math_lib = cbuild_subproject_get_target(math, "math");
    target_t* main;

    CBUILD_EXECUTABLE(main,
        CBUILD_SOURCES(main, "main.c");
        CBUILD_INCLUDES(main, "lib");
    );
    cbuild_target_link_library(main, math_lib);
    return cbuild_run(argc, argv);
}
