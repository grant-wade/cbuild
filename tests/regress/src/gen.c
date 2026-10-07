#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    FILE *file = fopen(argv[1], "w");
    if (!file) return 1;
    fputs("#ifndef REGRESS_GENERATED_H\n", file);
    fputs("#define REGRESS_GENERATED_H\n", file);
    fputs("#define GENERATED_VALUE 41\n", file);
    fputs("#endif\n", file);
    return fclose(file) == 0 ? 0 : 1;
}
