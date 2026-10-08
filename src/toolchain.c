/* toolchain.c - compiler, linker, and archiver detection. */

#include "cbuild_internal.h"

CBUILD_INTERNAL cbuild_cc_kind_t detect_cc_kind(const char* exe) {
    if (!exe) return CBUILD_CC_GCC_CLANG;
    const char* base = strrchr(exe, '/');
    if (!base) base = exe;
    const char* b2 = strrchr(base, '\\');
    if (b2) base = b2 + 1;
    if (strstr(base, "clang")) return CBUILD_CC_GCC_CLANG;
    if (strstr(base, "cl")) return CBUILD_CC_MSVC;
    if (strstr(base, "icl")) return CBUILD_CC_MSVC;
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
