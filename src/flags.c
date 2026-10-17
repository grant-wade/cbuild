/* flags.c - flag registration and dispatch. */

#include "cbuild_internal.h"

static void cbuild__register_flag(cbuild_context_t* ctx, cbuild_flag_handler_t fh) {
    if (ctx->flag_count + 1 > ctx->flag_cap) {
        ctx->flag_cap = ctx->flag_cap ? ctx->flag_cap * 2 : 8;
        ctx->flag_handlers = (cbuild_flag_handler_t*)realloc(
            ctx->flag_handlers, ctx->flag_cap * sizeof(*ctx->flag_handlers));
    }
    ctx->flag_handlers[ctx->flag_count++] = fh;
}

void cbuild_register_flag(cbuild_context_t* ctx, const char* long_name, char short_name, int takes_value,
                          cbuild_flag_phase_t phase, const char* help,
                          cbuild_flag_callback cb, void* user_data) {
    cbuild_flag_handler_t fh = (cbuild_flag_handler_t){ 0 };
    if (long_name) fh.long_name = strdup(long_name);
    fh.short_name = short_name;
    fh.takes_value = takes_value ? 1 : 0;
    fh.phase = phase;
    fh.help = help ? strdup(help) : NULL;
    fh.cb = cb;
    fh.user_data = user_data;
    cbuild__register_flag(ctx, fh);
}

void cbuild_register_flag_bool(cbuild_context_t* ctx, const char* long_name, char short_name,
                               cbuild_flag_phase_t phase, const char* help,
                               int* out_bool) {
    cbuild_flag_handler_t fh = (cbuild_flag_handler_t){ 0 };
    if (long_name) fh.long_name = strdup(long_name);
    fh.short_name = short_name;
    fh.phase = phase;
    fh.help = help ? strdup(help) : NULL;
    fh.bind_bool = out_bool;
    cbuild__register_flag(ctx, fh);
}

void cbuild_register_flag_int(cbuild_context_t* ctx, const char* long_name, char short_name,
                              cbuild_flag_phase_t phase, const char* help,
                              int* out_int) {
    cbuild_flag_handler_t fh = (cbuild_flag_handler_t){ 0 };
    if (long_name) fh.long_name = strdup(long_name);
    fh.short_name = short_name;
    fh.takes_value = 1;
    fh.phase = phase;
    fh.help = help ? strdup(help) : NULL;
    fh.bind_int = out_int;
    cbuild__register_flag(ctx, fh);
}

void cbuild_register_flag_str(cbuild_context_t* ctx, const char* long_name, char short_name,
                              cbuild_flag_phase_t phase, const char* help,
                              const char** out_str) {
    cbuild_flag_handler_t fh = (cbuild_flag_handler_t){ 0 };
    if (long_name) fh.long_name = strdup(long_name);
    fh.short_name = short_name;
    fh.takes_value = 1;
    fh.phase = phase;
    fh.help = help ? strdup(help) : NULL;
    fh.bind_str = out_str;
    cbuild__register_flag(ctx, fh);
}

/* Check if a flag is present in argv (for early/pre-cbuild_run parsing) */
int cbuild_has_flag(int argc, char** argv, const char* long_name, char short_name) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg) continue;

        /* Check --long_name */
        if (long_name && arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg + 2, long_name) == 0) return 1;
        }
        /* Check -X short flag */
        if (short_name && arg[0] == '-' && arg[1] != '-') {
            /* Could be -s or -abc (bundled) */
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == short_name) return 1;
            }
        }
    }
    return 0;
}

const char* cbuild_get_flag_value(int argc, char** argv, const char* long_name, char short_name) {
    const char* value = NULL;
    size_t long_len = long_name ? strlen(long_name) : 0;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg || arg[0] != '-') continue;
        if (strcmp(arg, "--") == 0) break;

        if (arg[1] == '-') {
            if (!long_name || strncmp(arg + 2, long_name, long_len) != 0) continue;
            const char* rest = arg + 2 + long_len;
            if (*rest == '=') {
                value = rest + 1;
            } else if (*rest == '\0' && i + 1 < argc) {
                value = argv[++i];
            }
        } else if (short_name && arg[1] == short_name) {
            if (arg[2]) {
                value = arg + 2;
            } else if (i + 1 < argc) {
                value = argv[++i];
            }
        }
    }
    return value;
}

CBUILD_INTERNAL void cbuild__print_help(cbuild_context_t* ctx) {
    /* One log call per line: the logger formats into a fixed-size buffer. */
    cbuild__log(ctx, CBUILD_LOG_INFO, "Usage: %s [flags] [subcommand]\n\nFlags:", ctx->argv0_for_help);
    const char* phase_names[] = { "Pre-parse", "Before build", "After build" };
    for (int ph = CBUILD_FLAG_PRE; ph <= CBUILD_FLAG_AFTER_BUILD; ++ph) {
        int header = 0;
        for (int i = 0; i < ctx->flag_count; ++i) {
            cbuild_flag_handler_t* fh = &ctx->flag_handlers[i];
            if (fh->phase != (cbuild_flag_phase_t)ph) continue;
            if (!header) {
                cbuild__log(ctx, CBUILD_LOG_INFO, "\n  %s:", phase_names[ph]);
                header = 1;
            }
            char short_part[8] = "";
            if (fh->short_name) snprintf(short_part, sizeof(short_part), "-%c%s", fh->short_name,
                                         fh->long_name ? ", " : "");
            cbuild__log(ctx, CBUILD_LOG_INFO, "    %s%s%s%s", short_part, fh->long_name ? "--" : "",
                        fh->long_name ? fh->long_name : "", fh->takes_value ? " <val>" : "");
            if (fh->help) cbuild__log(ctx, CBUILD_LOG_INFO, "        %s", fh->help);
        }
    }
    if (ctx->subcommand_count > 0) {
        cbuild__log(ctx, CBUILD_LOG_INFO, "\nSubcommands:");
        for (int i = 0; i < ctx->subcommand_count; ++i) {
            cbuild__log(ctx, CBUILD_LOG_INFO, "    %s", ctx->subcommands[i]->name);
        }
    }
}

/* The handler registered for a flag token in any phase, or NULL. For a short
   cluster, the first letter decides. */
static cbuild_flag_handler_t* cbuild__find_any_phase(cbuild_context_t* ctx, const char* arg) {
    if (arg[1] == '-') {
        const char* name = arg + 2;
        size_t n = strcspn(name, "=");
        for (int i = 0; i < ctx->flag_count; ++i) {
            cbuild_flag_handler_t* fh = &ctx->flag_handlers[i];
            if (fh->long_name && strlen(fh->long_name) == n && strncmp(fh->long_name, name, n) == 0) return fh;
        }
        return NULL;
    }
    for (int i = 0; i < ctx->flag_count; ++i) {
        if (ctx->flag_handlers[i].short_name == arg[1]) return &ctx->flag_handlers[i];
    }
    return NULL;
}

/* Whether a flag token takes the following argument as its value. */
static int cbuild__flag_uses_next(cbuild_flag_handler_t* fh, const char* arg) {
    if (!fh->takes_value) return 0;
    if (arg[1] == '-') return strchr(arg, '=') == NULL;
    return arg[2] == '\0';
}

/* Counts the arguments that are not flags (or flag values) left for a later
   phase, storing the index of the first in *first (-1 if none). */
CBUILD_INTERNAL int cbuild__positional_args(cbuild_context_t* ctx, int argc, char** argv, int* first) {
    int count = 0;
    int stop = 0;
    *first = -1;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (!stop && strcmp(arg, "--") == 0) {
            stop = 1;
            continue;
        }
        if (!stop && arg[0] == '-' && arg[1] != '\0') {
            cbuild_flag_handler_t* fh = cbuild__find_any_phase(ctx, arg);
            if (fh && cbuild__flag_uses_next(fh, arg)) i++;
            continue;
        }
        if (*first < 0) *first = i;
        count++;
    }
    return count;
}

/* strict dispatcher:
   - consumes handled flags from argv (compacting)
   - leaves flags registered for another phase, and their values, in place
   - errors on any unknown token that *looks* like a flag (- or --)
   - honors "--" to stop flag parsing (and keeps it for later phases)
*/
CBUILD_INTERNAL int cbuild_dispatch_flags_strict(cbuild_context_t* ctx, cbuild_flag_phase_t phase,
                                                 int* argc, char*** argvp) {
    int ac = *argc;
    char** av = *argvp;
    int write = 1;
    int stop = 0;

    for (int read = 1; read < ac; ++read) {
        const char* arg = av[read];

        if (stop || !arg || arg[0] != '-' || arg[1] == '\0') {
            av[write++] = av[read];
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            av[write++] = av[read];
            stop = 1;
            continue;
        }

        /* Another phase's flag waits for that phase's dispatch. */
        cbuild_flag_handler_t* other = cbuild__find_any_phase(ctx, arg);
        if (other && other->phase != phase) {
            av[write++] = av[read];
            if (cbuild__flag_uses_next(other, arg) && read + 1 < ac) av[write++] = av[++read];
            continue;
        }

        int handled = 0;
        const char* val = NULL;
        int consume_next = 0;

        if (arg[1] == '-') {
            /* --long or --long=value */
            const char* name = arg + 2;
            const char* eq = strchr(name, '=');
            char namebuf[256];
            if (eq) {
                size_t n = (size_t)(eq - name);
                    if (n >= sizeof(namebuf)) n = sizeof(namebuf) - 1;
                    memcpy(namebuf, name, n);
                    namebuf[n] = 0;
                    val = eq + 1;
                    name = namebuf;
                }
                for (int i = 0; i < ctx->flag_count; ++i) {
                    cbuild_flag_handler_t* fh = &ctx->flag_handlers[i];
                if (fh->phase != phase) continue;
                if (!fh->long_name || strcmp(fh->long_name, name) != 0) continue;

                if (fh->takes_value && !val) {
                    if (read + 1 < ac) {
                        val = av[read + 1];
                        consume_next = 1;
                    } else {
                        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: --%s requires a value", name);
                        return 1;
                    }
                }

                if (fh->bind_bool) *fh->bind_bool = 1;
                if (fh->bind_int && val) *fh->bind_int = atoi(val);
                if (fh->bind_str && val) {
                    /* Free any previously allocated string */
                    if (fh->allocated_str) free(fh->allocated_str);
                    fh->allocated_str = strdup(val);
                    *fh->bind_str = fh->allocated_str;
                }

                int rc = fh->cb ? fh->cb(val, fh->user_data) : 0;
                handled = 1;
                if (consume_next) read++;
                if (rc == CBUILD_FLAG_EXIT) return CBUILD_FLAG_EXIT;
                if (rc) return rc;
                break;
            }
        } else {
            /* -xyz or -j8 or -j 8 */
            const char* p = arg + 1;
            while (*p && !handled) {
                char s = *p++;
                cbuild_flag_handler_t* fh = NULL;
                for (int i = 0; i < ctx->flag_count; ++i) {
                    if (ctx->flag_handlers[i].phase == phase &&
                        ctx->flag_handlers[i].short_name == s) {
                        fh = &ctx->flag_handlers[i];
                        break;
                    }
                }
                if (!fh) {
                    cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: unknown flag -%c", s);
                    return 1;
                }

                if (fh->takes_value) {
                    if (*p)
                        val = p;
                    else if (read + 1 < ac) {
                        val = av[read + 1];
                        consume_next = 1;
                    } else {
                        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: -%c requires a value", s);
                        return 1;
                    }
                }

                if (fh->bind_bool) *fh->bind_bool = 1;
                if (fh->bind_int && val) *fh->bind_int = atoi(val);
                if (fh->bind_str && val) {
                    /* Free any previously allocated string */
                    if (fh->allocated_str) free(fh->allocated_str);
                    fh->allocated_str = strdup(val);
                    *fh->bind_str = fh->allocated_str;
                }

                int rc = fh->cb ? fh->cb(val, fh->user_data) : 0;
                handled = 1;
                if (consume_next) read++;
                if (rc == CBUILD_FLAG_EXIT) return CBUILD_FLAG_EXIT;
                if (rc) return rc;
                break; /* stop cluster if one took a value */
            }
        }

        if (!handled) {
            cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: unknown option '%s'", arg);
            return 1;
        }
    }

    av[write] = NULL;
    *argc = write;
    return 0;
}
