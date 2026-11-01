#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

void api_setup(void);
void header_setup(void);
void regress_setup(void);
void runner_setup(void);
void project_setup(void);
void subproject_setup(void);
void example_setup(void);

typedef struct {
    const char *name;
    void (*setup)(void);
    void (*run)(void);
} scenario_t;

static const scenario_t scenarios[] = {
#define X(group, name) { #group "." #name, group##_setup, group##_##name },
    SCENARIOS(X)
#undef X
};

/* Prints args[1] to stdout and args[2] to stderr when present, then exits with args[0]. */
static int run_child(int argc, char **args) {
    if (argc > 1) printf("%s\n", args[1]);
    if (argc > 2) fprintf(stderr, "%s\n", args[2]);
    return atoi(args[0]);
}

/* usage: suite SCENARIO SCRATCH_NAME */
int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--child") == 0) return run_child(argc - 2, argv + 2);
    if (argc >= 4 && strcmp(argv[1], "--fake-compiler") == 0) return fake_compiler(argc - 2, argv + 2);
    if (argc != 3) {
        fprintf(stderr, "usage: %s SCENARIO SCRATCH_NAME\n", argv[0]);
        return 2;
    }
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        if (strcmp(scenarios[i].name, argv[1]) != 0) continue;
        h_init(argv[0], argv[2]);
        scenarios[i].setup();
        scenarios[i].run();
        h_finish();
        return 0;
    }
    fprintf(stderr, "no scenario named %s\n", argv[1]);
    return 2;
}
