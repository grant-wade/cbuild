/* config.c - build configurations and presets. */

#include "cbuild_internal.h"

CBUILD_INTERNAL void cbuild__apply_config_if_needed(cbuild_context_t* ctx, target_t* t) {
    if (t->config_applied) return;
    do {
        config_t* chain[3];
        int n = 0;

        /* Find target-specific config first */
        config_t* tcfg = NULL;
        for (int i = 0; i < ctx->cfg_count; ++i) {
            if (ctx->cfg_targets[i] == t) {
                tcfg = ctx->cfg_values[i];
                break;
            }
        }

        /* Build config chain: default -> (active OR target) -> target
         * If target has its own config, skip active_config to avoid flag duplication */
        chain[n++] = ctx->default_config;
        if (tcfg) {
            /* Target has explicit config - use it instead of active config */
            chain[n++] = tcfg;
        } else {
            /* No target config - use active config as fallback */
            chain[n++] = ctx->active_config;
        }

        for (int ci = 0; ci < n; ++ci) {
            config_t* cfg = chain[ci];
            if (!cfg) continue;

            /* Apply tool overrides */
            if (cfg->compiler) {
                if (t->compiler) free(t->compiler);
                t->compiler = strdup(cfg->compiler);
                t->cc_kind_override = (int)detect_cc_kind(t->compiler);
            }
            if (cfg->linker) {
                if (t->linker) free(t->linker);
                t->linker = strdup(cfg->linker);
            }

            /* Tokenized vectors */
            for (int i = 0; i < cfg->nincludes; ++i) {
                ensure_capacity_charpp(ctx, &t->include_dirs, &t->include_count, &t->include_cap);
                t->include_dirs[t->include_count++] = strdup(cfg->includes[i]);
            }
            for (int i = 0; i < cfg->ndefines; ++i) {
                ensure_capacity_charpp(ctx, &t->defines, &t->define_count, &t->define_cap);
                t->defines[t->define_count++] = strdup(cfg->defines[i]);
            }
            for (int i = 0; i < cfg->ncflags; ++i) {
                ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                t->cflags[t->cflag_count++] = strdup(cfg->cflags[i]);
            }
            for (int i = 0; i < cfg->nldflags; ++i) {
                ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                t->ldflags[t->ldflag_count++] = strdup(cfg->ldflags[i]);
            }
            for (int i = 0; i < cfg->nlibdirs; ++i) {
                ensure_capacity_charpp(ctx, &t->lib_dirs, &t->lib_dir_count, &t->lib_dir_cap);
                t->lib_dirs[t->lib_dir_count++] = strdup(cfg->libdirs[i]);
            }
            for (int i = 0; i < cfg->nlinklibs; ++i) {
                ensure_capacity_charpp(ctx, &t->link_libs, &t->link_lib_count, &t->link_lib_cap);
                t->link_libs[t->link_lib_count++] = strdup(cfg->linklibs[i]);
            }

            /* Structured knobs → flags */
            /* Use target's cc_kind_override if set, otherwise fall back to context */
            cbuild_cc_kind_t effective_cc_kind = (t->cc_kind_override != -1)
                ? (cbuild_cc_kind_t)t->cc_kind_override : ctx->cc_kind;

            if (cfg->opt_level >= 0) {
                if (effective_cc_kind == CBUILD_CC_MSVC) {
                    const char* f = (cfg->opt_level <= 0)   ? "/Od"
                                    : (cfg->opt_level == 1) ? "/O1"
                                                            : "/O2";
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup(f);
                } else {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "-O%d", cfg->opt_level);
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup(buf);
                }
            }

            if (cfg->debug_symbols >= 0) {
                if (cfg->debug_symbols) {
                    if (effective_cc_kind == CBUILD_CC_MSVC) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("/Zi");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("/DEBUG");
                    } else {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-g");
                    }
                }
            }

            if (cfg->lto >= 0) {
                if (effective_cc_kind == CBUILD_CC_MSVC) {
                    if (cfg->lto) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("/GL");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("/LTCG");
                    }
                } else {
                    if (cfg->lto == 0) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-fno-lto");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("-fno-lto");
                    } else if (cfg->lto > 0) {
                        const char* cflag = (cfg->lto == 2) ? "-flto=thin" : "-flto";
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup(cflag);
                        const char* ldflag = (cfg->lto == 2) ? "-flto=thin" : "-flto";
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup(ldflag);
                    }
                }
            }

            if (cfg->pic >= 0) {
                if (cfg->pic && effective_cc_kind != CBUILD_CC_MSVC) {
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup("-fPIC");
                }
            }

            if (cfg->warnings >= 0) {
                if (effective_cc_kind == CBUILD_CC_MSVC) {
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup("/W4");
                } else {
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup("-Wall");
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup("-Wextra");
                    if (cfg->warnings >= 2) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-Wpedantic");
                    }
                }
            }

            if (cfg->std && *cfg->std) {
                if (effective_cc_kind == CBUILD_CC_MSVC) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "/std:%s", cfg->std);
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup(buf);
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "-std=%s", cfg->std);
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup(buf);
                }
            }

            if (cfg->runtime && *cfg->runtime) {
                if (effective_cc_kind == CBUILD_CC_MSVC) {
                    const char* f = (strcmp(cfg->runtime, "static") == 0) ? "/MT" : "/MD";
                    ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                    t->cflags[t->cflag_count++] = strdup(f);
                }
            }

            if (cfg->freestanding >= 0) {
                if (cfg->freestanding) {
                    if (effective_cc_kind == CBUILD_CC_MSVC) {
                        /* Omit default library name and default libraries */
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("/Zl");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("/NODEFAULTLIB");
                    } else {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-ffreestanding");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("-nostdlib");
                    }
                }
            }

            if (cfg->sanitize) {
                if (effective_cc_kind != CBUILD_CC_MSVC) {
                    if (cfg->sanitize & 1) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-fsanitize=address");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("-fsanitize=address");
                    }
                    if (cfg->sanitize & 2) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-fsanitize=undefined");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("-fsanitize=undefined");
                    }
                    if (cfg->sanitize & 4) {
                        ensure_capacity_charpp(ctx, &t->cflags, &t->cflag_count, &t->cflag_cap);
                        t->cflags[t->cflag_count++] = strdup("-fsanitize=thread");
                        ensure_capacity_charpp(ctx, &t->ldflags, &t->ldflag_count, &t->ldflag_cap);
                        t->ldflags[t->ldflag_count++] = strdup("-fsanitize=thread");
                    }
                }
            }
        }
    } while (0);
    t->config_applied = 1;
}

/* ---------------- Build Configuration API (implementation) ---------------- */

static void cbuild__config_add_tokens(cbuild_context_t* ctx, char*** arr, int* count, int* cap, const char* flags) {
    if (!flags || !*flags) return;
    char* copy = strdup(flags);
    if (!copy) return;

    char* p = copy;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        char* start = p;
        int in_quote = 0;
        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                p++;
            } else if (!in_quote && (*p == ' ' || *p == '\t'))
                break;
            else
                p++;
        }
        if (p > start) {
            char saved = *p;
            *p = '\0';

            char* token = start;
            size_t len = strlen(token);
            if (len >= 2 && token[0] == '"' && token[len - 1] == '"') {
                token[len - 1] = '\0';
                token++;
            }

            ensure_capacity_charpp(ctx, arr, count, cap);
            (*arr)[(*count)++] = strdup(token);

            *p = saved;
        }
    }
    free(copy);
}

config_t* cbuild_config_new(cbuild_context_t* ctx, const char* name) {
    config_t* c = (config_t*)calloc(1, sizeof(config_t));
    if (!c) return NULL;
    c->name = name ? strdup(name) : NULL;

    /* Track this config in the context for automatic cleanup */
    if (ctx) {
        if (ctx->all_configs_count >= ctx->all_configs_cap) {
            int newcap = ctx->all_configs_cap ? ctx->all_configs_cap * 2 : 4;
            config_t** newarr = (config_t**)realloc(ctx->all_configs, newcap * sizeof(config_t*));
            if (newarr) {
                ctx->all_configs = newarr;
                ctx->all_configs_cap = newcap;
            }
        }
        if (ctx->all_configs_count < ctx->all_configs_cap) {
            ctx->all_configs[ctx->all_configs_count++] = c;
        }
    }
    c->cflags = NULL;
    c->ncflags = 0;
    c->cflags_cap = 0;
    c->ldflags = NULL;
    c->nldflags = 0;
    c->ldflags_cap = 0;
    c->defines = NULL;
    c->ndefines = 0;
    c->defines_cap = 0;
    c->includes = NULL;
    c->nincludes = 0;
    c->includes_cap = 0;
    c->libdirs = NULL;
    c->nlibdirs = 0;
    c->libdirs_cap = 0;
    c->linklibs = NULL;
    c->nlinklibs = 0;
    c->linklibs_cap = 0;

    c->opt_level = -1;
    c->debug_symbols = -1;
    c->lto = -1;
    c->pic = -1;
    c->warnings = -1;
    c->output_dir = NULL;
    c->std = NULL;
    c->runtime = NULL;
    c->sanitize = 0;
    c->compiler = NULL;
    c->linker = NULL;
    return c;
}

void cbuild_config_free(cbuild_context_t* ctx, config_t* c) {
    if (!c) return;

    /* Remove from tracking array if present */
    if (ctx && ctx->all_configs) {
        for (int i = 0; i < ctx->all_configs_count; ++i) {
            if (ctx->all_configs[i] == c) {
                ctx->all_configs[i] = NULL;
                break;
            }
        }
    }
    if (c->cflags) {
        for (int i = 0; i < c->ncflags; ++i) free(c->cflags[i]);
        free(c->cflags);
    }
    if (c->ldflags) {
        for (int i = 0; i < c->nldflags; ++i) free(c->ldflags[i]);
        free(c->ldflags);
    }
    if (c->defines) {
        for (int i = 0; i < c->ndefines; ++i) free(c->defines[i]);
        free(c->defines);
    }
    if (c->includes) {
        for (int i = 0; i < c->nincludes; ++i) free(c->includes[i]);
        free(c->includes);
    }
    if (c->libdirs) {
        for (int i = 0; i < c->nlibdirs; ++i) free(c->libdirs[i]);
        free(c->libdirs);
    }
    if (c->linklibs) {
        for (int i = 0; i < c->nlinklibs; ++i) free(c->linklibs[i]);
        free(c->linklibs);
    }
    if (c->name) free((void*)c->name);
    if (c->output_dir) free((void*)c->output_dir);
    if (c->std) free((void*)c->std);
    if (c->runtime) free((void*)c->runtime);
    if (c->compiler) free((void*)c->compiler);
    if (c->linker) free((void*)c->linker);
    free(c);
}

void cbuild_config_add_cflags(cbuild_context_t* ctx, config_t* c, const char* flags) {
    if (!c) return;
    cbuild__config_add_tokens(ctx, &c->cflags, &c->ncflags, &c->cflags_cap, flags);
}

void cbuild_config_add_ldflags(cbuild_context_t* ctx, config_t* c, const char* flags) {
    if (!c) return;
    cbuild__config_add_tokens(ctx, &c->ldflags, &c->nldflags, &c->ldflags_cap, flags);
}

void cbuild_config_add_define(cbuild_context_t* ctx, config_t* c, const char* def) {
    (void)ctx;
    if (!c || !def) return;
    ensure_capacity_charpp(ctx, &c->defines, &c->ndefines, &c->defines_cap);
    c->defines[c->ndefines++] = strdup(def);
}

void cbuild_config_add_include(cbuild_context_t* ctx, config_t* c, const char* dir) {
    (void)ctx;
    if (!c || !dir) return;
    ensure_capacity_charpp(ctx, &c->includes, &c->nincludes, &c->includes_cap);
    /* Normalize path separators for consistent path handling on Windows */
    c->includes[c->nincludes++] = cbuild__normalize_path(dir);
}

void cbuild_config_add_libdir(cbuild_context_t* ctx, config_t* c, const char* dir) {
    (void)ctx;
    if (!c || !dir) return;
    ensure_capacity_charpp(ctx, &c->libdirs, &c->nlibdirs, &c->libdirs_cap);
    /* Normalize path separators for consistent path handling on Windows */
    c->libdirs[c->nlibdirs++] = cbuild__normalize_path(dir);
}

void cbuild_config_add_linklib(cbuild_context_t* ctx, config_t* c, const char* lib) {
    (void)ctx;
    if (!c || !lib) return;
    ensure_capacity_charpp(ctx, &c->linklibs, &c->nlinklibs, &c->linklibs_cap);
    /* Normalize path separators for consistent path handling on Windows */
    c->linklibs[c->nlinklibs++] = cbuild__normalize_path(lib);
}

void cbuild_config_set_opt(cbuild_context_t* ctx, config_t* c, int level) {
    (void)ctx;
    if (!c) return;
    c->opt_level = level;
}

void cbuild_config_set_debug(cbuild_context_t* ctx, config_t* c, int on) {
    (void)ctx;
    if (!c) return;
    c->debug_symbols = on ? 1 : 0;
}

void cbuild_config_set_lto(cbuild_context_t* ctx, config_t* c, int mode) {
    (void)ctx;
    if (!c) return;
    c->lto = mode;
}

void cbuild_config_set_pic(cbuild_context_t* ctx, config_t* c, int on) {
    (void)ctx;
    if (!c) return;
    c->pic = on ? 1 : 0;
}

void cbuild_config_set_warnings(cbuild_context_t* ctx, config_t* c, int mode) {
    (void)ctx;
    if (!c) return;
    c->warnings = mode;
}

void cbuild_config_set_std(cbuild_context_t* ctx, config_t* c, const char* std) {
    (void)ctx;
    if (!c) return;
    if (c->std) {
        free((void*)c->std);
        c->std = NULL;
    }
    c->std = std ? strdup(std) : NULL;
}

void cbuild_config_set_runtime(cbuild_context_t* ctx, config_t* c, const char* runtime) {
    (void)ctx;
    if (!c) return;
    if (c->runtime) {
        free((void*)c->runtime);
        c->runtime = NULL;
    }
    c->runtime = runtime ? strdup(runtime) : NULL;
}

void cbuild_config_set_output_dir(cbuild_context_t* ctx, config_t* c, const char* output_dir) {
    (void)ctx;
    if (!c) return;
    if (c->output_dir) {
        free((void*)c->output_dir);
        c->output_dir = NULL;
    }
    /* Normalize path separators for consistent path handling on Windows */
    c->output_dir = output_dir ? cbuild__normalize_path(output_dir) : NULL;
}

void cbuild_config_enable_sanitizers(cbuild_context_t* ctx, config_t* c, int mask) {
    (void)ctx;
    if (!c) return;
    c->sanitize |= mask;
}

void cbuild_config_disable_sanitizers(cbuild_context_t* ctx, config_t* c, int mask) {
    (void)ctx;
    if (!c) return;
    c->sanitize &= ~mask;
}

void cbuild_config_set_compiler(cbuild_context_t* ctx, config_t* c, const char* compiler) {
    (void)ctx;
    if (!c) return;
    if (c->compiler) {
        free((void*)c->compiler);
        c->compiler = NULL;
    }
    c->compiler = compiler ? strdup(compiler) : NULL;
}

void cbuild_config_set_linker(cbuild_context_t* ctx, config_t* c, const char* linker) {
    (void)ctx;
    if (!c) return;
    if (c->linker) {
        free((void*)c->linker);
        c->linker = NULL;
    }
    c->linker = linker ? strdup(linker) : NULL;
}

void cbuild_set_active_config(cbuild_context_t* ctx, config_t* c) {
    ctx->active_config = c;
}

void cbuild_target_set_config(cbuild_context_t* ctx, target_t* t, config_t* c) {
    if (!t) return;
    for (int i = 0; i < ctx->cfg_count; ++i) {
        if (ctx->cfg_targets[i] == t) {
            ctx->cfg_values[i] = c;
            return;
        }
    }
    if (ctx->cfg_count + 1 > ctx->cfg_cap) {
        ctx->cfg_cap = ctx->cfg_cap ? ctx->cfg_cap * 2 : 8;
        ctx->cfg_targets = (target_t**)realloc(ctx->cfg_targets, ctx->cfg_cap * sizeof(*ctx->cfg_targets));
        ctx->cfg_values = (config_t**)realloc(ctx->cfg_values, ctx->cfg_cap * sizeof(*ctx->cfg_values));
    }
    ctx->cfg_targets[ctx->cfg_count] = t;
    ctx->cfg_values[ctx->cfg_count] = c;
    ctx->cfg_count++;
}

/* ---------------- Pre-baked platform-aware standard configs ---------------- */

config_t* cbuild_config_default_debug(cbuild_context_t* ctx) {
    config_t* c = cbuild_config_new(ctx, "Debug");
    if (!c) return NULL;

    cbuild_config_set_output_dir(ctx, c, "build/debug");
    cbuild_config_set_std(ctx, c, "c23");
    cbuild_config_set_warnings(ctx, c, 2); /* -Wall -Wextra -Wpedantic or /W4 */
    cbuild_config_set_opt(ctx, c, 0);
    cbuild_config_set_debug(ctx, c, 1);

#ifdef _WIN32
    /* MSVC-specific niceties */
    cbuild_config_add_cflags(ctx, c, "/nologo");
    cbuild_config_add_cflags(ctx, c, "/WX");      /* treat warnings as errors */
    cbuild_config_set_runtime(ctx, c, "dynamic"); /* /MD by default */
#else
    /* GCC/Clang on Linux/macOS */
    cbuild_config_enable_sanitizers(ctx, c, 1 | 2); /* ASAN | UBSAN */
    cbuild_config_add_cflags(ctx, c, "-Werror");    /* treat warnings as errors */
#endif
    return c;
}

config_t* cbuild_config_default_release(cbuild_context_t* ctx) {
    config_t* c = cbuild_config_new(ctx, "Release");
    if (!c) return NULL;

    cbuild_config_set_output_dir(ctx, c, "build/release");
    cbuild_config_set_std(ctx, c, "c23");
    cbuild_config_set_warnings(ctx, c, 2); /* -Wall -Wextra -Wpedantic or /W4 */
    cbuild_config_set_opt(ctx, c, 3);
    cbuild_config_set_debug(ctx, c, 0);

#ifdef _WIN32
    /* MSVC-specific niceties */
    cbuild_config_add_cflags(ctx, c, "/nologo");
    cbuild_config_add_cflags(ctx, c, "/WX");      /* treat warnings as errors */
    cbuild_config_set_runtime(ctx, c, "dynamic"); /* /MD by default */
#else
    /* GCC/Clang on Linux/macOS */
    cbuild_config_add_cflags(ctx, c, "-Werror"); /* treat warnings as errors */
#endif
    return c;
}

config_t* cbuild_config_default_wasm32(cbuild_context_t* ctx) {
    config_t* c = cbuild_config_new(ctx, "WASM32");
    if (!c) return NULL;

    cbuild_config_set_std(ctx, c, "c23");
    cbuild_config_set_warnings(ctx, c, 2); /* -Wall -Wextra -Wpedantic or /W4 */
    cbuild_config_set_opt(ctx, c, 3);
    cbuild_config_set_debug(ctx, c, 0);

    cbuild_config_add_cflags(ctx, c, "--target=wasm32");
    cbuild_config_add_ldflags(ctx, c, "--target=wasm32");
    cbuild_config_add_ldflags(ctx, c, "-nostdlib");

    return c;
}

void cbuild_config_set_freestanding(cbuild_context_t* ctx, config_t* c, int on) {
    (void)ctx;
    if (!c) return;
    c->freestanding = on ? 1 : 0;
}
