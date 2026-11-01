#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#include <stdio.h>

/* A compiler string and whether it takes MSVC-style arguments. */
typedef struct {
    const char *compiler;
    int msvc;
} compiler_kind_t;

static const compiler_kind_t kinds[] = {
    { "cl", 1 },
    { "cl.exe", 1 },
    { "CL.EXE", 1 },
    { "icl", 1 },
    { "clang-cl", 1 },
    { "clang-cl.exe", 1 },
    { "/opt/msvc/bin/cl", 1 },
    { "C:\\VS\\bin\\cl.exe", 1 },
    { "\"C:\\Program Files\\VS\\cl.exe\" /nologo", 1 },
    { "cl -DTARGET_CC=1", 1 },
    { "cl /I/usr/include", 1 },
    { "sccache cl", 1 },
    { "ccache /opt/msvc/bin/cl.exe", 1 },

    { "cc", 0 },
    { "gcc", 0 },
    { "clang", 0 },
    { "clang.exe", 0 },
    { "clang-18", 0 },
    { "tcc", 0 },
    { "zig cc", 0 },
    { "zig cc -target x86_64-linux-musl", 0 },
    { "/opt/cl/bin/gcc", 0 },
    { "C:\\cl\\gcc.exe", 0 },
    { "cc -I/usr/include", 0 },
    { "gcc -Ivendor/include -DUSE_CL=1", 0 },
    { "clever-cc", 0 },
    { "x86_64-cl-linux-gcc", 0 },
    { "\"C:\\Program Files\\cl\\gcc.exe\" -O2", 0 },
    { "ccache gcc", 0 },
    { "sccache clang", 0 },
    { "ccache", 0 },
    { "", 0 },
};

int main(void) {
    int failures = 0;
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        int msvc = detect_cc_kind(kinds[i].compiler) == CBUILD_CC_MSVC;
        if (msvc != kinds[i].msvc) {
            printf("'%s' detected as %s\n", kinds[i].compiler, msvc ? "MSVC" : "GCC-style");
            failures++;
        }
    }
    if (detect_cc_kind(NULL) == CBUILD_CC_MSVC) {
        printf("NULL detected as MSVC\n");
        failures++;
    }
    return failures ? 1 : 0;
}
