/* command.c - custom commands and subcommands. */

#include "cbuild_internal.h"

command_t* cbuild_command(cbuild_context_t* ctx, const char* name, const char* command_line) {
    command_t* cmd = (command_t*)calloc(1, sizeof(command_t));
    cmd->name = strdup(name);
    cmd->command_line = command_line ? strdup(command_line) : NULL;
    cmd->argv = NULL;
    cmd->argc = 0;
    ensure_capacity_charpp(ctx, (char***)&ctx->commands, &ctx->command_count,
                           &ctx->command_cap);
    ctx->commands[ctx->command_count++] = cmd;
    return cmd;
}

command_t* cbuild_command_argv(cbuild_context_t* ctx, const char* name, char** argv, int argc) {
    if (!name || !argv || argc <= 0)
        return NULL;

    command_t* cmd = (command_t*)calloc(1, sizeof(command_t));
    cmd->name = strdup(name);
    cmd->command_line = NULL;
    cmd->argc = argc;
    cmd->argv = (char**)calloc(argc + 1, sizeof(char*));
    for (int i = 0; i < argc; ++i) {
        cmd->argv[i] = strdup(argv[i]);
    }
    cmd->argv[argc] = NULL; /* NULL-terminate for execvp */

    ensure_capacity_charpp(ctx, (char***)&ctx->commands, &ctx->command_count,
                           &ctx->command_cap);
    ctx->commands[ctx->command_count++] = cmd;

    return cmd;
}

void cbuild_target_add_command(cbuild_context_t* ctx, target_t* target, command_t* cmd) {
    (void)ctx;
    if (!target || !cmd)
        return;
    ensure_capacity_charpp(ctx, (char***)&target->commands, &target->cmd_count,
                           &target->cmd_cap);
    target->commands[target->cmd_count++] = cmd;
}

void cbuild_target_add_post_command(cbuild_context_t* ctx, target_t* target, command_t* cmd) {
    if (!target || !cmd)
        return;
    ensure_capacity_charpp(ctx, (char***)&target->post_commands,
                           &target->post_cmd_count, &target->post_cmd_cap);
    target->post_commands[target->post_cmd_count++] = cmd;
}

void cbuild_command_add_dependency(cbuild_context_t* ctx, command_t* cmd, command_t* dependency) {
    if (!cmd || !dependency)
        return;
    ensure_capacity_charpp(ctx, (char***)&cmd->dependencies, &cmd->dep_count,
                           &cmd->dep_cap);
    cmd->dependencies[cmd->dep_count++] = dependency;
}

int cbuild_run_command(cbuild_context_t* ctx, command_t* cmd) {
    if (!cmd)
        return -1;
    if (cmd->executed)
        return cmd->result;
    if (cmd->in_progress) {
        cbuild__set_error(ctx, "Circular command dependency involving '%s'", cmd->name);
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: circular command dependency involving %s", cmd->name);
        return -1;
    }
    cmd->in_progress = 1;
    for (int i = 0; i < cmd->dep_count; ++i) {
        int rc = cbuild_run_command(ctx, cmd->dependencies[i]);
        if (rc != 0) {
            cmd->in_progress = 0;
            return rc;
        }
    }

    cbuild__log_step(ctx, "COMMAND", CBUILD_COLOR_MAGENTA, "%s", cmd->name);

    int rc = 0;

    if (cmd->callback) {
        cmd->callback(cmd->user_data);
        rc = 0;
    } else if (cmd->argv && cmd->argc > 0) {
        /* Argv-based command: shell-free execution */
        cbuild_argv_t argv;
        cbuild_argv_init(&argv);
        for (int i = 0; i < cmd->argc; ++i) {
            cbuild_argv_append(&argv, cmd->argv[i]);
        }
        rc = cbuild_spawn_process(ctx, &argv, 0, NULL);
        cbuild_argv_free(&argv);
    } else if (cmd->command_line) {
        /* Note: user-defined command lines may use shell syntax, so we keep run_command here */
        rc = run_command(ctx, cmd->command_line, 0, NULL);
    } else {
        cbuild__log_status(ctx, 0, "No command or callback in command: %s", cmd->name);
        rc = -1;
    }

    cmd->executed = 1;
    cmd->in_progress = 0;
    cmd->result = rc;

    if (rc != 0) {
        cbuild__log_status(ctx, 0, "Command failed: %s", cmd->name);
    }

    return rc;
}

void cbuild_register_subcommand(cbuild_context_t* ctx, const char* name, target_t* target,
                                const char* command_line,
                                cbuild_subcommand_callback callback,
                                void* user_data) {
    cbuild_subcommand_t* scmd =
        (cbuild_subcommand_t*)calloc(1, sizeof(cbuild_subcommand_t));
    scmd->name = strdup(name);
    scmd->target = target;
    scmd->command_line = command_line ? strdup(command_line) : NULL;
    scmd->callback = callback;
    scmd->user_data = user_data;
    ensure_capacity_charpp(ctx, (char***)&ctx->subcommands, &ctx->subcommand_count,
                           &ctx->subcommand_cap);
    ctx->subcommands[ctx->subcommand_count++] = scmd;
}

command_t* cbuild_command_function(cbuild_context_t* ctx, const char* name,
                                   cbuild_subcommand_callback callback,
                                   void* user_data) {
    if (!name || !callback)
        return NULL;

    command_t* cmd = (command_t*)calloc(1, sizeof(command_t));
    cmd->name = strdup(name);
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->callback = callback;
    cmd->user_data = user_data;

    ensure_capacity_charpp(ctx, (char***)&ctx->commands, &ctx->command_count,
                           &ctx->command_cap);
    ctx->commands[ctx->command_count++] = cmd;

    return cmd;
}
