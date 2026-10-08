/* util.c - small string and array helpers. */

#include "cbuild_internal.h"

CBUILD_INTERNAL void cbuild__trim(char* s) {
    char* end;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++;
    end = s + strlen(s) - 1;
    while (end > s &&
           (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = 0;
}

CBUILD_INTERNAL int ensure_capacity_charpp(cbuild_context_t* ctx, char*** arr, int* count, int* capacity) {
    if (*count + 1 > *capacity) {
        int newcap = *capacity ? *capacity * 2 : 4;
        char** newarr = (char**)realloc(*arr, newcap * sizeof(char*));
        if (!newarr) {
            cbuild__set_error(ctx, "Out of memory allocating %d pointers", newcap);
            cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Out of memory");
            return -1;
        }
        *arr = newarr;
        *capacity = newcap;
    }
    return 0;
}

CBUILD_INTERNAL int append_format(char** dst, const char* fmt, ...) {
    va_list args;
    va_list args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int add_len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (add_len < 0) {
        va_end(args);
        return -1;
    }
    size_t old_len = *dst ? strlen(*dst) : 0;
    char* newptr = (char*)realloc(*dst, old_len + add_len + 1);
    if (!newptr) {
        va_end(args);
        return -1;
    }
    *dst = newptr;
    vsnprintf(*dst + old_len, add_len + 1, fmt, args);
    va_end(args);
    return 0;
}

// --- small helpers for file suffix checks ------------------------------------
CBUILD_INTERNAL int cbuild__has_suffix(const char* s, const char* suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lt = strlen(suf);
    if (lt > ls) return 0;
    return strcasecmp(s + (ls - lt), suf) == 0;
}

CBUILD_INTERNAL unsigned cbuild__hash_path(const char* s) {
    unsigned h = 5381u;
    int c;
    while ((c = (unsigned char)*s++)) {
        h = ((h << 5) + h) ^ (unsigned)c; /* h*33 ^ c */
    }
    return h;
}

CBUILD_INTERNAL void fprint_json_string(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; ++s) {
        switch (*s) {
            case '\\':
                fputs("\\\\", f);
                break;
            case '"':
                fputs("\\\"", f);
                break;
            case '\b':
                fputs("\\b", f);
                break;
            case '\f':
                fputs("\\f", f);
                break;
            case '\n':
                fputs("\\n", f);
                break;
            case '\r':
                fputs("\\r", f);
                break;
            case '\t':
                fputs("\\t", f);
                break;
            default:
                if ((unsigned char)*s < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned char)*s);
                } else {
                    fputc(*s, f);
                }
        }
    }
    fputc('"', f);
}
