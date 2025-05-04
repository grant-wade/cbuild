#define CBUILD_IMPLEMENTATION
#include "../cbuild.h"


int main(int argc, char** argv) {
    CBUILD_SELF_REBUILD("build.c", "cbuild.h");
    cbuild_set_output_dir("build");
    target_t* math;
    CBUILD_STATIC_LIBRARY(math, CBUILD_SOURCES(math, "math.c", "math.h"););
    return cbuild_run(argc, argv);
}
