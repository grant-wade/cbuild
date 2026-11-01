#include <stdlib.h>

#include "harness.h"

void header_setup(void) {}

SCENARIO(header, declarations_compile_as_c) {
    names_t command = split_flags(getenv("CC"));
#ifdef _WIN32
    const char *flags = "/nologo /std:c11 /W4 /D_CRT_SECURE_NO_WARNINGS /c";
    const char *include = format_text("/I%s", root_path(""));
    const char *output = "/Fo:declarations.obj";
#else
    const char *cflags = getenv("CFLAGS");
    const char *flags = format_text("%s -c", cflags && *cflags ? cflags : "-std=c11 -Wall -Wextra -Wpedantic -Werror");
    const char *include = format_text("-I%s", root_path(""));
    const char *output = "-odeclarations.o";
#endif
    names_t split = split_flags(flags);
    for (int i = 0; i < split.count; ++i) names_add(&command, split.items[i]);
    names_add(&command, include);
    names_add(&command, output);
    names_add(&command, root_path("tests/fixtures/header/declarations.c"));
    CHECK(h_run_names(command).status == 0, "cbuild.h did not compile as C");
}

SCENARIO(header, declarations_compile_as_cpp) {
    const char *compiler = getenv("CXX");
#ifdef _WIN32
    if (!compiler || !*compiler) compiler = "cl";
    const char *flags = "/nologo /std:c++14 /W4 /D_CRT_SECURE_NO_WARNINGS /TP /c";
    const char *include = format_text("/I%s", root_path(""));
    const char *output = "/Fo:declarations.obj";
#else
    if (!compiler || !*compiler) compiler = "c++";
    const char *cxxflags = getenv("CXXFLAGS");
    const char *flags = format_text("%s -c", cxxflags && *cxxflags ? cxxflags : "-std=c++11 -Wall -Wextra -Wpedantic -Werror");
    const char *include = format_text("-I%s", root_path(""));
    const char *output = "-odeclarations.o";
#endif
    names_t command = split_flags(compiler);
    names_t split = split_flags(flags);
    for (int i = 0; i < split.count; ++i) names_add(&command, split.items[i]);
    names_add(&command, include);
    names_add(&command, output);
    names_add(&command, root_path("tests/fixtures/header/declarations.cpp"));
    CHECK(h_run_names(command).status == 0, "cbuild.h did not compile as C++");
}
