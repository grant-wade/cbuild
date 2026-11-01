/* log.c - logging and error reporting. */

#include "cbuild_internal.h"

#define CBUILD_SECTION_RULE "\xe2\x94\x80"

#if defined(_WIN32) && !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

enum { CBUILD_SECTION_WIDTH = 48 };

CBUILD_INTERNAL int cbuild__is_nested(void) {
    const char* nested = getenv("CBUILD_NESTED");
    return nested && *nested;
}

static int cbuild__use_color(FILE* out) {
    const char* no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return 0;
    const char* force = getenv("CLICOLOR_FORCE");
    if (force && *force && strcmp(force, "0") != 0) return 1;
    int fd = fileno(out);
    if (!isatty(fd)) return 0;
#ifdef _WIN32
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return 0;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return 1;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    const char* term = getenv("TERM");
    return !term || strcmp(term, "dumb") != 0;
#endif
}

/* step_color applies only to CBUILD_LOG_STEP messages. */
static void cbuild__print_log(cbuild_log_level_t level, const char* msg, const char* step_color) {
    FILE* out = (level == CBUILD_LOG_ERROR || level == CBUILD_LOG_WARNING || level == CBUILD_LOG_STATUS_FAIL) ? stderr : stdout;
    int color = cbuild__use_color(out);
    const char* reset = color ? CBUILD_COLOR_RESET : "";
    switch (level) {
        case CBUILD_LOG_INFO:
            fprintf(out, "%s\n", msg);
            break;
        case CBUILD_LOG_WARNING:
            fprintf(out, "%sWarning:%s %s\n", color ? CBUILD_COLOR_YELLOW : "", reset, msg);
            break;
        case CBUILD_LOG_ERROR:
            fprintf(out, "%sError:%s %s\n", color ? CBUILD_COLOR_RED : "", reset, msg);
            break;
        case CBUILD_LOG_STEP: {
            const char* paint = color ? step_color : "";
            const char* space = strchr(msg, ' ');
            if (space) {
                int label_len = (int)(space - msg);
                fprintf(out, "%s%-10.*s%s %s\n", paint, label_len, msg, reset, space + 1);
            } else {
                fprintf(out, "%s%-10s%s\n", paint, msg, reset);
            }
            break;
        }
        case CBUILD_LOG_STATUS_OK:
            fprintf(out, "%s%s%s %s\n", color ? CBUILD_COLOR_GREEN : "", "✔", reset, msg);
            break;
        case CBUILD_LOG_STATUS_FAIL:
            fprintf(out, "%s%s%s %s\n", color ? CBUILD_COLOR_RED : "", "✖", reset, msg);
            break;
        case CBUILD_LOG_VERBOSE:
            fprintf(out, "%s\n", msg);
            break;
        case CBUILD_LOG_SECTION:
            fprintf(out, "%s" CBUILD_SECTION_RULE CBUILD_SECTION_RULE " %s ", color ? CBUILD_COLOR_BOLD : "", msg);
            for (int i = (int)strlen(msg) + 4; i < CBUILD_SECTION_WIDTH; ++i) fputs(CBUILD_SECTION_RULE, out);
            fprintf(out, "%s\n", reset);
            break;
    }
    fflush(out);
}

static void cbuild__default_logger(void* user_data, cbuild_log_level_t level, const char* msg) {
    (void)user_data;
    cbuild__print_log(level, msg, CBUILD_COLOR_BLUE);
}

CBUILD_INTERNAL void cbuild__log(cbuild_context_t* ctx, cbuild_log_level_t level, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cbuild_log_fn fn = ctx->log_fn ? ctx->log_fn : cbuild__default_logger;
    fn(ctx->log_user_data, level, buf);
}

static void cbuild__emit_step(cbuild_context_t* ctx, const char* label, const char* color, const char* text) {
    char msg[2048];
    snprintf(msg, sizeof(msg), "%s ", label);
    size_t used = strlen(msg);
    strncat(msg, text, sizeof(msg) - used - 1);
    if (ctx->log_fn) {
        ctx->log_fn(ctx->log_user_data, CBUILD_LOG_STEP, msg);
    } else {
        cbuild__print_log(CBUILD_LOG_STEP, msg, color);
    }
}

CBUILD_INTERNAL void cbuild__log_step(cbuild_context_t* ctx, const char* label, const char* color, const char* fmt, ...) {
    if (!cbuild__is_nested()) {
        if (!ctx->section_started) cbuild__log_section(ctx, "Setup");
    } else if (!ctx->nested_announced) {
        ctx->nested_announced = 1;
        char announcement[512];
        snprintf(announcement, sizeof(announcement), "build subproject %s", getenv("CBUILD_NESTED"));
        cbuild__emit_step(ctx, "COMMAND", CBUILD_COLOR_MAGENTA, announcement);
    }
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cbuild__emit_step(ctx, label, color, buf);
}

CBUILD_INTERNAL void cbuild__log_status(cbuild_context_t* ctx, int ok, const char* fmt, ...) {
    if (ok && cbuild__is_nested()) return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cbuild_log_fn fn = ctx->log_fn ? ctx->log_fn : cbuild__default_logger;
    fn(ctx->log_user_data, ok ? CBUILD_LOG_STATUS_OK : CBUILD_LOG_STATUS_FAIL, buf);
}

CBUILD_INTERNAL void cbuild__log_section(cbuild_context_t* ctx, const char* fmt, ...) {
    if (cbuild__is_nested()) return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (ctx->section_started && !ctx->log_fn) fputc('\n', stdout);
    ctx->section_started = 1;
    cbuild_log_fn fn = ctx->log_fn ? ctx->log_fn : cbuild__default_logger;
    fn(ctx->log_user_data, CBUILD_LOG_SECTION, buf);
}

void cbuild_set_logger(cbuild_context_t* ctx, cbuild_log_fn callback, void* user_data) {
    ctx->log_fn = callback;
    ctx->log_user_data = user_data;
}

const char* cbuild_get_last_error(cbuild_context_t* ctx) {
    if (!ctx) return "NULL context";
    return ctx->last_error[0] ? ctx->last_error : NULL;
}

CBUILD_INTERNAL void cbuild__set_error(cbuild_context_t* ctx, const char* fmt, ...) {
    if (!ctx) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->last_error, sizeof(ctx->last_error), fmt, args);
    va_end(args);
}
