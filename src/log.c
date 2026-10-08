/* log.c - logging and error reporting. */

#include "cbuild_internal.h"

static void cbuild__default_logger(void* user_data, cbuild_log_level_t level, const char* msg) {
    (void)user_data;
    FILE* out = (level == CBUILD_LOG_ERROR || level == CBUILD_LOG_WARNING || level == CBUILD_LOG_STATUS_FAIL) ? stderr : stdout;
    switch (level) {
        case CBUILD_LOG_INFO:
            fprintf(out, "%s\n", msg);
            break;
        case CBUILD_LOG_WARNING:
            fprintf(out, "%sWarning:%s %s\n", CBUILD_COLOR_YELLOW, CBUILD_COLOR_RESET, msg);
            break;
        case CBUILD_LOG_ERROR:
            fprintf(out, "%sError:%s %s\n", CBUILD_COLOR_RED, CBUILD_COLOR_RESET, msg);
            break;
        case CBUILD_LOG_STEP: {
            const char* space = strchr(msg, ' ');
            if (space) {
                int label_len = (int)(space - msg);
                fprintf(out, "%s%-10.*s%s %s\n", CBUILD_COLOR_BLUE, label_len, msg, CBUILD_COLOR_RESET, space + 1);
            } else {
                fprintf(out, "%s%-10s%s\n", CBUILD_COLOR_BLUE, msg, CBUILD_COLOR_RESET);
            }
            break;
        }
        case CBUILD_LOG_STATUS_OK:
            fprintf(out, "%s%s%s %s\n", CBUILD_COLOR_GREEN, "✔", CBUILD_COLOR_RESET, msg);
            break;
        case CBUILD_LOG_STATUS_FAIL:
            fprintf(out, "%s%s%s %s\n", CBUILD_COLOR_RED, "✖", CBUILD_COLOR_RESET, msg);
            break;
        case CBUILD_LOG_VERBOSE:
            fprintf(out, "%s\n", msg);
            break;
    }
    fflush(out);
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

CBUILD_INTERNAL void cbuild__log_step(cbuild_context_t* ctx, const char* label, const char* color, const char* fmt, ...) {
    (void)color;
    char buf[2048];
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    snprintf(msg, sizeof(msg), "%s ", label);
    size_t used = strlen(msg);
    strncat(msg, buf, sizeof(msg) - used - 1);
    cbuild_log_fn fn = ctx->log_fn ? ctx->log_fn : cbuild__default_logger;
    fn(ctx->log_user_data, CBUILD_LOG_STEP, msg);
}

CBUILD_INTERNAL void cbuild__log_status(cbuild_context_t* ctx, int ok, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cbuild_log_fn fn = ctx->log_fn ? ctx->log_fn : cbuild__default_logger;
    fn(ctx->log_user_data, ok ? CBUILD_LOG_STATUS_OK : CBUILD_LOG_STATUS_FAIL, buf);
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
