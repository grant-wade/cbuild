/*
 * amalgamate.c - generate the single-header cbuild.h from the sources in src/.
 *
 *     cc tools/amalgamate.c -o amalgamate
 *     ./amalgamate            # rewrite cbuild.h
 *     ./amalgamate --check    # exit 1 if cbuild.h is out of date
 *
 * Run from the repository root, or pass --src DIR and --out FILE.
 *
 * Output layout: src/cbuild.h (the public API) verbatim, then, inside
 * #ifdef CBUILD_IMPLEMENTATION, every other src/ header followed by every
 * src/ .c file, each group in byte-wise name order. A quoted #include that
 * names a file in src/ is expanded in place the first time and dropped after
 * that; every other line is copied unchanged. The result depends only on the
 * contents of src/, so regenerating is always reproducible.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define PUBLIC_HEADER "cbuild.h"

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} buf_t;

typedef struct {
    char** names;
    int count;
    int cap;
} list_t;

static const char* src_dir = "src";
static list_t emitted;

static void die(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "amalgamate: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

static void buf_append(buf_t* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = (char*)realloc(b->data, b->cap);
        if (!b->data) die("out of memory");
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_puts(buf_t* b, const char* s) {
    buf_append(b, s, strlen(s));
}

static void list_add(list_t* l, const char* name) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->names = (char**)realloc(l->names, (size_t)l->cap * sizeof(char*));
        if (!l->names) die("out of memory");
    }
    l->names[l->count] = (char*)malloc(strlen(name) + 1);
    if (!l->names[l->count]) die("out of memory");
    strcpy(l->names[l->count++], name);
}

static void list_free(list_t* l) {
    for (int i = 0; i < l->count; ++i) free(l->names[i]);
    free(l->names);
    memset(l, 0, sizeof(*l));
}

static int list_has(const list_t* l, const char* name) {
    for (int i = 0; i < l->count; ++i)
        if (strcmp(l->names[i], name) == 0) return 1;
    return 0;
}

static int cmp_names(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

/* Read a whole file with CRLF folded to LF so that checkouts with converted
   line endings generate and compare identically. Returns 0 if unreadable. */
static int read_file(const char* path, buf_t* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char chunk[65536];
    size_t n;
    out->len = 0;
    buf_append(out, "", 0);
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        size_t w = 0;
        for (size_t i = 0; i < n; ++i)
            if (chunk[i] != '\r') chunk[w++] = chunk[i];
        buf_append(out, chunk, w);
    }
    fclose(f);
    return 1;
}

static char* src_path(const char* name) {
    char* path = (char*)malloc(strlen(src_dir) + strlen(name) + 2);
    if (!path) die("out of memory");
    sprintf(path, "%s/%s", src_dir, name);
    return path;
}

/* Names (not paths) of the regular .c and .h files directly inside src_dir. */
static void list_sources(list_t* headers, list_t* sources) {
#ifdef _WIN32
    char* pattern = src_path("*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) die("cannot list %s", src_dir);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (has_suffix(fd.cFileName, ".h")) list_add(headers, fd.cFileName);
        if (has_suffix(fd.cFileName, ".c")) list_add(sources, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    free(pattern);
#else
    DIR* d = opendir(src_dir);
    if (!d) die("cannot list %s", src_dir);
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (has_suffix(e->d_name, ".h")) list_add(headers, e->d_name);
        if (has_suffix(e->d_name, ".c")) list_add(sources, e->d_name);
    }
    closedir(d);
#endif
    if (headers->count)
        qsort(headers->names, (size_t)headers->count, sizeof(char*), cmp_names);
    if (sources->count)
        qsort(sources->names, (size_t)sources->count, sizeof(char*), cmp_names);
}

/* If the line is `#include "name"`, copy name into out and return 1. */
static int parse_quoted_include(const char* line, size_t len, char* out, size_t out_size) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '#') return 0;
    i++;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (len - i < 7 || strncmp(line + i, "include", 7) != 0) return 0;
    i += 7;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '"') return 0;
    size_t start = ++i;
    while (i < len && line[i] != '"') i++;
    if (i >= len || i - start == 0 || i - start >= out_size) return 0;
    memcpy(out, line + start, i - start);
    out[i - start] = 0;
    return 1;
}

static void emit_file(buf_t* out, const char* name) {
    char* path = src_path(name);
    buf_t in = {0};
    if (!read_file(path, &in)) die("cannot read %s", path);
    list_add(&emitted, name);

    size_t pos = 0;
    while (pos < in.len) {
        const char* line = in.data + pos;
        const char* nl = (const char*)memchr(line, '\n', in.len - pos);
        size_t len = nl ? (size_t)(nl - line) : in.len - pos;
        pos += len + (nl ? 1 : 0);

        char inc[256];
        if (parse_quoted_include(line, len, inc, sizeof(inc))) {
            if (list_has(&emitted, inc)) continue;
            char* inc_path = src_path(inc);
            FILE* probe = fopen(inc_path, "rb");
            free(inc_path);
            if (probe) {
                fclose(probe);
                emit_file(out, inc);
                continue;
            }
        }
        buf_append(out, line, len);
        buf_append(out, "\n", 1);
    }
    free(in.data);
    free(path);
}

static void emit_marked(buf_t* out, const char* name) {
    if (list_has(&emitted, name)) return;
    buf_puts(out, "\n/* ---- ");
    buf_puts(out, src_dir);
    buf_puts(out, "/");
    buf_puts(out, name);
    buf_puts(out, " ---- */\n\n");
    emit_file(out, name);
}

static void generate(buf_t* out) {
    list_t headers = {0}, sources = {0};
    list_sources(&headers, &sources);
    if (!list_has(&headers, PUBLIC_HEADER)) die("%s/%s not found", src_dir, PUBLIC_HEADER);
    if (sources.count == 0) die("no .c files found in %s", src_dir);

    buf_puts(out, "/* GENERATED FILE - DO NOT EDIT.\n"
                  "   Amalgamated from ");
    buf_puts(out, src_dir);
    buf_puts(out, "/ by tools/amalgamate.c; edit those files and regenerate. */\n");
    emit_file(out, PUBLIC_HEADER);

    buf_puts(out, "\n"
                  "/* ---------------------------------------------------------------------- */\n"
                  "/* Implementation below (define CBUILD_IMPLEMENTATION in one source file) */\n"
                  "/* ---------------------------------------------------------------------- */\n"
                  "#ifdef CBUILD_IMPLEMENTATION\n");
    for (int i = 0; i < headers.count; ++i) emit_marked(out, headers.names[i]);
    for (int i = 0; i < sources.count; ++i) emit_marked(out, sources.names[i]);
    buf_puts(out, "\n#endif /* CBUILD_IMPLEMENTATION */\n");
    list_free(&headers);
    list_free(&sources);
    list_free(&emitted);
}

/* 1-based line of the first difference between two buffers. */
static int first_diff_line(const buf_t* a, const buf_t* b) {
    int line = 1;
    for (size_t i = 0; i < a->len && i < b->len; ++i) {
        if (a->data[i] != b->data[i]) return line;
        if (a->data[i] == '\n') line++;
    }
    return line;
}

int main(int argc, char** argv) {
    const char* out_path = "cbuild.h";
    int check = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) {
            check = 1;
        } else if (strcmp(argv[i], "--src") == 0 && i + 1 < argc) {
            src_dir = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--check] [--src DIR] [--out FILE]\n", argv[0]);
            return 2;
        }
    }

    buf_t generated = {0};
    generate(&generated);

    buf_t existing = {0};
    int have_existing = read_file(out_path, &existing);
    int same = have_existing && existing.len == generated.len &&
               memcmp(existing.data, generated.data, generated.len) == 0;

    int rc = 0;
    if (same) {
        /* up to date; when writing, this also leaves the file's mtime alone */
    } else if (check) {
        rc = 1;
        if (!have_existing)
            fprintf(stderr, "amalgamate: %s does not exist.\n", out_path);
        else
            fprintf(stderr, "amalgamate: %s is out of date with %s/ (first difference at line %d).\n",
                    out_path, src_dir, first_diff_line(&existing, &generated));
        fprintf(stderr,
                "%s is generated: edit the files in %s/, then regenerate and commit it:\n"
                "    cc tools/amalgamate.c -o amalgamate && ./amalgamate\n"
                "Enable the pre-commit hook to do this automatically:\n"
                "    git config core.hooksPath .githooks\n",
                out_path, src_dir);
    } else {
        FILE* f = fopen(out_path, "wb");
        if (!f) die("cannot write %s", out_path);
        if (fwrite(generated.data, 1, generated.len, f) != generated.len || fclose(f) != 0)
            die("failed writing %s", out_path);
        printf("amalgamate: wrote %s\n", out_path);
    }

    free(generated.data);
    free(existing.data);
    return rc;
}
