#define CBUILD_IMPLEMENTATION
#include "../../cbuild.h"

int main(int argc, char **argv) {
  CBUILD_SELF_REBUILD("build.c", "cbuild.h");
  cbuild_set_output_dir("build");
  target_t *math;
  // example: wildcard expansion of *.c and *.h
  CBUILD_STATIC_LIBRARY(math, CBUILD_SOURCES(math, "src/*.c", "src/*.h"););
  return cbuild_run(argc, argv);
}
