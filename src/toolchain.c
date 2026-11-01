/* toolchain.c - compiler, linker, and archiver detection. */

#include "cbuild_internal.h"

/* Copies the file name of the next word of *text, lowercased and without .exe, and advances
 * *text past the word. A word may be double-quoted. Returns 0 at the end or if name is too small. */
static int cbuild__next_program_name(const char** text, char* name, size_t name_size) {
    const char* at = *text;
    while (*at == ' ' || *at == '\t') at++;
    if (!*at) return 0;

    int quoted = *at == '"';
    if (quoted) at++;
    const char* start = at;
    while (*at && (quoted ? *at != '"' : (*at != ' ' && *at != '\t'))) at++;
    const char* end = at;
    if (quoted && *at) at++;
    *text = at;

    for (const char* p = start; p < end; ++p) {
        if (*p == '/' || *p == '\\') start = p + 1;
    }
    size_t length = (size_t)(end - start);
    if (length >= name_size) return 0;
    for (size_t i = 0; i < length; ++i) name[i] = (char)tolower((unsigned char)start[i]);
    if (length > 4 && strncmp(name + length - 4, ".exe", 4) == 0) length -= 4;
    name[length] = '\0';
    return 1;
}

CBUILD_INTERNAL cbuild_cc_kind_t detect_cc_kind(const char* exe) {
    if (!exe) return CBUILD_CC_GCC_CLANG;
    char name[128];
    const char* at = exe;
    do {
        if (!cbuild__next_program_name(&at, name, sizeof(name))) return CBUILD_CC_GCC_CLANG;
    } while (strcmp(name, "ccache") == 0 || strcmp(name, "sccache") == 0);
    if (strcmp(name, "cl") == 0 || strcmp(name, "clang-cl") == 0 || strcmp(name, "icl") == 0) return CBUILD_CC_MSVC;
    return CBUILD_CC_GCC_CLANG;
}

/* Detect if archiver is MSVC-style (lib.exe) or Unix-style (ar, zig ar, llvm-ar, etc.) */
CBUILD_INTERNAL cbuild_cc_kind_t detect_ar_kind(const char* ar) {
    if (!ar) return CBUILD_CC_GCC_CLANG;
    const char* base = strrchr(ar, '/');
    if (!base) base = ar;
    const char* b2 = strrchr(base, '\\');
    if (b2) base = b2 + 1;
    /* MSVC archiver is "lib" or "lib.exe" */
    if (strcmp(base, "lib") == 0 || strcmp(base, "lib.exe") == 0) return CBUILD_CC_MSVC;
    /* Everything else (ar, zig ar, llvm-ar, etc.) is Unix-style */
    return CBUILD_CC_GCC_CLANG;
}

/* Helper to resolve compiler for a target */
CBUILD_INTERNAL const char* cbuild_target_compiler(cbuild_context_t* ctx, target_t* t) {
    return t->compiler ? t->compiler : ctx->cc;
}

CBUILD_INTERNAL cbuild_cc_kind_t cbuild_target_cc_kind(cbuild_context_t* ctx, target_t* t) {
    return (t->cc_kind_override != -1) ? (cbuild_cc_kind_t)t->cc_kind_override : ctx->cc_kind;
}

CBUILD_INTERNAL const char* cbuild_target_linker(cbuild_context_t* ctx, target_t* t) {
    return t->linker ? t->linker : ctx->ld;
}

void cbuild_guess_compiler(cbuild_context_t* ctx) {
    #if defined(_WIN32)
        cbuild_set_compiler(ctx, "cl");
    #elif defined(__APPLE__)
        cbuild_set_compiler(ctx, "clang");
    #else
        cbuild_set_compiler(ctx, "cc");
    #endif
}
