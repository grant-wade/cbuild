#define CBUILD_IMPLEMENTATION
#include "../cbuild.h"

int init_dep() {
    if (!cbuild_file_exists("lib/cbuild") && cbuild_file_exists("lib/build.c")) {
        // we need to run gcc -o lib/cbuild lib/cbuild.c
        command_t* cmd = cbuild_command("build_cbuild", "gcc -o lib/cbuild lib/build.c");
        int status = cbuild_run_command(cmd);
        if (status != 0) {
            fprintf(stderr, "Failed to build cbuild dependency\n");
            return status;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
  CBUILD_SELF_REBUILD("build.c", "cbuild.h");
  cbuild_set_output_dir("build");
  cbuild_enable_compile_commands(1);
  if (init_dep() != 0) {
      fprintf(stderr, "Failed to initialize dependencies\n");
      return 1;
  }
  CBUILD_SUBPROJECT(math, "lib", "./cbuild");
  target_t *math_lib = cbuild_subproject_get_target(math, "math");
  target_t *main;
  CBUILD_EXECUTABLE(main, CBUILD_SOURCES(main, "main.c");
                    CBUILD_INCLUDES(main, "lib/src"););
  cbuild_target_link_library(main, math_lib);
  return cbuild_run(argc, argv);
}
