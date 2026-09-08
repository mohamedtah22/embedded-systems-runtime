#define _POSIX_C_SOURCE 200809L
#include "shell.h"
#include "parser.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t sigchld_seen = 0;

static void sigchld_handler(int signo) {
    (void)signo;
    sigchld_seen = 1;
}

static void install_shell_signals(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);

    struct sigaction chld = {0};
    chld.sa_handler = sigchld_handler;
    sigemptyset(&chld.sa_mask);
    chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &chld, NULL);
}

static void restore_child_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

int shell_state_init(ShellState *state, int interactive) {
    memset(state, 0, sizeof(*state));
    jobs_init(&state->jobs);
    state->history_base = 1;
    state->interactive = interactive;
    state->terminal_fd = STDIN_FILENO;
    state->shell_pgid = getpgrp();

    if (interactive) {
        while (tcgetpgrp(state->terminal_fd) != (state->shell_pgid = getpgrp())) {
            kill(-state->shell_pgid, SIGTTIN);
        }
        install_shell_signals();
        state->shell_pgid = getpid();
        if (setpgid(state->shell_pgid, state->shell_pgid) < 0 && errno != EACCES && errno != EPERM) {
            perror("setpgid(shell)");
            return -1;
        }
        if (tcsetpgrp(state->terminal_fd, state->shell_pgid) < 0) {
            perror("tcsetpgrp(shell)");
            return -1;
        }
    }
    return 0;
}

void shell_state_destroy(ShellState *state) {
    if (!state) return;
    for (size_t i = 0; i < state->history_count; ++i) {
        free(state->history[i]);
    }
    jobs_destroy(&state->jobs);
    memset(state, 0, sizeof(*state));
}

static void history_add(ShellState *state, const char *line) {
    if (!line || !*line) return;
    char *copy = mlrt_xstrdup(line);
    size_t len = strlen(copy);
    while (len && (copy[len - 1] == '\n' || copy[len - 1] == '\r')) copy[--len] = '\0';
    if (!*copy) {
        free(copy);
        return;
    }
    if (state->history_count == SHELL_HISTORY_MAX) {
        free(state->history[0]);
        memmove(&state->history[0], &state->history[1], (SHELL_HISTORY_MAX - 1) * sizeof(state->history[0]));
        state->history_count--;
        state->history_base++;
    }
    state->history[state->history_count++] = copy;
}

static const char *history_lookup(ShellState *state, unsigned long number) {
    if (number < state->history_base) return NULL;
    unsigned long index = number - state->history_base;
    if (index >= state->history_count) return NULL;
    return state->history[index];
}

static char *history_expand(ShellState *state, const char *line, int err_fd) {
    while (*line == ' ' || *line == '\t') ++line;
    size_t len = strcspn(line, "\r\n");
    if (len == 2 && strncmp(line, "!!", 2) == 0) {
        if (!state->history_count) {
            dprintf(err_fd, "history: no previous command\n");
            return NULL;
        }
        return mlrt_xstrdup(state->history[state->history_count - 1]);
    }
    if (len > 1 && line[0] == '!') {
        char buf[64];
        if (len >= sizeof(buf)) {
            dprintf(err_fd, "history: event specifier too long\n");
            return NULL;
        }
        memcpy(buf, line + 1, len - 1);
        buf[len - 1] = '\0';
        char *end = NULL;
        errno = 0;
        unsigned long n = strtoul(buf, &end, 10);
        if (errno || !end || *end) {
            dprintf(err_fd, "history: invalid event '%.*s'\n", (int)len, line);
            return NULL;
        }
        const char *entry = history_lookup(state, n);
        if (!entry) {
            dprintf(err_fd, "history: event %lu not found\n", n);
            return NULL;
        }
        return mlrt_xstrdup(entry);
    }
    return mlrt_xstrdup(line);
}

static int redirect_file(const char *path, int target_fd, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd < 0) return -1;
    if (dup2(fd, target_fd) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    close(fd);
    return 0;
}

static int setup_command_fds(const ShellPipeline *pipeline, size_t index, int (*pipes)[2], int out_fd, int err_fd) {
    const ShellCommand *cmd = &pipeline->commands[index];
    if (index > 0) {
        if (dup2(pipes[index - 1][0], STDIN_FILENO) < 0) return -1;
    }
    if (index + 1 < pipeline->count) {
        if (dup2(pipes[index][1], STDOUT_FILENO) < 0) return -1;
    } else if (out_fd >= 0 && out_fd != STDOUT_FILENO) {
        if (dup2(out_fd, STDOUT_FILENO) < 0) return -1;
    }
    if (err_fd >= 0 && err_fd != STDERR_FILENO) {
        if (dup2(err_fd, STDERR_FILENO) < 0) return -1;
    }

    if (cmd->input_path && redirect_file(cmd->input_path, STDIN_FILENO, O_RDONLY, 0) < 0) return -1;
    if (cmd->output_path) {
        int flags = O_WRONLY | O_CREAT | (cmd->append_output ? O_APPEND : O_TRUNC);
        if (redirect_file(cmd->output_path, STDOUT_FILENO, flags, 0666) < 0) return -1;
    }
    return 0;
}

static void close_all_pipes(int (*pipes)[2], size_t pipe_count) {
    for (size_t i = 0; i < pipe_count; ++i) {
        if (pipes[i][0] >= 0) close(pipes[i][0]);
        if (pipes[i][1] >= 0) close(pipes[i][1]);
    }
}

static void wait_foreground_job(ShellState *state, Job *job, int err_fd) {
    int status;
    while (job->remaining > 0 && job->state != JOB_STOPPED) {
        pid_t pid = waitpid(-job->pgid, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno != ECHILD) dprintf(err_fd, "waitpid: %s\n", strerror(errno));
            break;
        }
        jobs_update_status(&state->jobs, pid, status);
    }

    if (state->interactive) {
        if (tcsetpgrp(state->terminal_fd, state->shell_pgid) < 0) {
            dprintf(err_fd, "tcsetpgrp(shell): %s\n", strerror(errno));
        }
    }

    if (job->state == JOB_DONE) {
        jobs_remove(&state->jobs, job);
    } else if (job->state == JOB_STOPPED) {
        dprintf(STDOUT_FILENO, "[%d] Stopped  %s\n", job->id, job->command);
    }
}

static int execute_external_pipeline(ShellState *state, const ShellPipeline *pipeline, int out_fd, int err_fd, int track_jobs) {
    size_t pipe_count = pipeline->count > 0 ? pipeline->count - 1 : 0;
    int (*pipes)[2] = pipe_count ? mlrt_xcalloc(pipe_count, sizeof(*pipes)) : NULL;
    for (size_t i = 0; i < pipe_count; ++i) {
        pipes[i][0] = pipes[i][1] = -1;
        if (pipe(pipes[i]) < 0) {
            dprintf(err_fd, "pipe: %s\n", strerror(errno));
            close_all_pipes(pipes, i);
            free(pipes);
            return 1;
        }
    }

    pid_t *pids = mlrt_xcalloc(pipeline->count, sizeof(*pids));
    pid_t pgid = 0;
    size_t spawned = 0;

    for (size_t i = 0; i < pipeline->count; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            dprintf(err_fd, "fork: %s\n", strerror(errno));
            if (pgid > 0) kill(-pgid, SIGTERM);
            close_all_pipes(pipes, pipe_count);
            for (size_t j = 0; j < spawned; ++j) waitpid(pids[j], NULL, 0);
            free(pipes);
            free(pids);
            return 1;
        }
        if (pid == 0) {
            restore_child_signals();
            pid_t child_pid = getpid();
            pid_t child_pgid = pgid ? pgid : child_pid;
            if (setpgid(0, child_pgid) < 0 && errno != EACCES) {
                dprintf(STDERR_FILENO, "setpgid: %s\n", strerror(errno));
                _exit(126);
            }
            if (setup_command_fds(pipeline, i, pipes, out_fd, err_fd) < 0) {
                dprintf(STDERR_FILENO, "redirection: %s\n", strerror(errno));
                _exit(126);
            }
            close_all_pipes(pipes, pipe_count);
            execvp(pipeline->commands[i].argv[0], pipeline->commands[i].argv);
            dprintf(STDERR_FILENO, "%s: %s\n", pipeline->commands[i].argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }

        if (!pgid) pgid = pid;
        if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH) {
            dprintf(err_fd, "setpgid(%d): %s\n", (int)pid, strerror(errno));
        }
        pids[spawned++] = pid;
    }

    close_all_pipes(pipes, pipe_count);
    free(pipes);

    if (!track_jobs) {
        int last_status = 0;
        for (size_t i = 0; i < spawned; ++i) {
            int status = 0;
            while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR) {}
            if (i + 1 == spawned) last_status = status;
        }
        free(pids);
        if (WIFEXITED(last_status)) return WEXITSTATUS(last_status);
        if (WIFSIGNALED(last_status)) return 128 + WTERMSIG(last_status);
        return 1;
    }

    Job *job = jobs_add(&state->jobs, pgid, pids, spawned, pipeline->source, JOB_RUNNING);
    free(pids);

    if (pipeline->background) {
        dprintf(out_fd, "[%d] %d\n", job->id, (int)job->pgid);
        return 0;
    }

    if (state->interactive) {
        if (tcsetpgrp(state->terminal_fd, pgid) < 0) {
            dprintf(err_fd, "tcsetpgrp(job): %s\n", strerror(errno));
        }
    }
    wait_foreground_job(state, job, err_fd);
    return 0;
}

static int builtin_history(ShellState *state, const ShellCommand *cmd, int out_fd) {
    unsigned long start = state->history_base;
    size_t begin = 0;
    if (cmd->argc == 2) {
        char *end = NULL;
        long n = strtol(cmd->argv[1], &end, 10);
        if (end && *end == '\0' && n >= 0 && (size_t)n < state->history_count) {
            begin = state->history_count - (size_t)n;
            start += begin;
        }
    }
    for (size_t i = begin; i < state->history_count; ++i) {
        dprintf(out_fd, "%5lu  %s\n", start + (i - begin), state->history[i]);
    }
    return 0;
}

static int parse_job_id(const ShellCommand *cmd, int *id, int err_fd) {
    if (cmd->argc < 2) {
        dprintf(err_fd, "%s: expected job id (for example %%1 or 1)\n", cmd->argv[0]);
        return -1;
    }
    const char *text = cmd->argv[1][0] == '%' ? cmd->argv[1] + 1 : cmd->argv[1];
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!end || *end || value <= 0) {
        dprintf(err_fd, "%s: invalid job id '%s'\n", cmd->argv[0], cmd->argv[1]);
        return -1;
    }
    *id = (int)value;
    return 0;
}

static int run_builtin(ShellState *state, const ShellPipeline *pipeline, int out_fd, int err_fd, int *handled) {
    *handled = 0;
    if (pipeline->count != 1) return 0;
    const ShellCommand *cmd = &pipeline->commands[0];
    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0) {
        *handled = 1;
        const char *path = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        if (!path || chdir(path) < 0) {
            dprintf(err_fd, "cd: %s\n", path ? strerror(errno) : "HOME is not set");
            return 1;
        }
        return 0;
    }
    if (strcmp(name, "exit") == 0 || strcmp(name, "quit") == 0) {
        *handled = 1;
        state->should_exit = 1;
        return 0;
    }
    if (strcmp(name, "history") == 0) {
        *handled = 1;
        return builtin_history(state, cmd, out_fd);
    }
    if (strcmp(name, "jobs") == 0) {
        *handled = 1;
        jobs_print(&state->jobs, out_fd);
        return 0;
    }
    if (strcmp(name, "fg") == 0 || strcmp(name, "bg") == 0) {
        *handled = 1;
        jobs_reap(&state->jobs, -1);
        int id;
        if (parse_job_id(cmd, &id, err_fd) != 0) return 1;
        Job *job = jobs_find_id(&state->jobs, id);
        if (!job || job->state == JOB_DONE) {
            dprintf(err_fd, "%s: job %d not found\n", name, id);
            return 1;
        }
        if (strcmp(name, "bg") == 0) {
            if (kill(-job->pgid, SIGCONT) < 0) {
                dprintf(err_fd, "bg: SIGCONT: %s\n", strerror(errno));
                return 1;
            }
            job->state = JOB_RUNNING;
            dprintf(out_fd, "[%d] Running  %s\n", job->id, job->command);
            return 0;
        }
        if (state->interactive && tcsetpgrp(state->terminal_fd, job->pgid) < 0) {
            dprintf(err_fd, "fg: tcsetpgrp: %s\n", strerror(errno));
            return 1;
        }
        if (kill(-job->pgid, SIGCONT) < 0 && errno != ESRCH) {
            dprintf(err_fd, "fg: SIGCONT: %s\n", strerror(errno));
        }
        job->state = JOB_RUNNING;
        wait_foreground_job(state, job, err_fd);
        return 0;
    }
    if (strcmp(name, "help") == 0) {
        *handled = 1;
        dprintf(out_fd,
                "builtins: cd exit quit history jobs fg bg help\n"
                "syntax: command [args] [< file] [> file|>> file] [| command ...] [&]\n");
        return 0;
    }
    return 0;
}

int shell_run_line(ShellState *state, const char *line, int out_fd, int err_fd) {
    if (sigchld_seen) {
        jobs_reap(&state->jobs, state->interactive ? out_fd : -1);
        sigchld_seen = 0;
    }

    char *expanded = history_expand(state, line, err_fd);
    if (!expanded) return 1;
    size_t expanded_len = strlen(expanded);
    while (expanded_len && (expanded[expanded_len - 1] == '\n' || expanded[expanded_len - 1] == '\r')) {
        expanded[--expanded_len] = '\0';
    }
    if (!*expanded) {
        free(expanded);
        return 0;
    }

    ShellPipeline pipeline;
    char parse_error[256];
    if (shell_parse_line(expanded, &pipeline, parse_error, sizeof(parse_error)) != 0) {
        dprintf(err_fd, "parse: %s\n", parse_error);
        free(expanded);
        return 2;
    }
    if (pipeline.count == 0) {
        shell_pipeline_free(&pipeline);
        free(expanded);
        return 0;
    }

    history_add(state, expanded);
    if (strcmp(expanded, line) != 0 && state->interactive) dprintf(out_fd, "%s\n", expanded);

    int handled = 0;
    int rc = run_builtin(state, &pipeline, out_fd, err_fd, &handled);
    if (!handled) {
        rc = execute_external_pipeline(state, &pipeline, out_fd, err_fd, 1);
    }
    shell_pipeline_free(&pipeline);
    free(expanded);
    return rc;
}

int shell_run_noninteractive(const char *line, int out_fd, int err_fd) {
    ShellPipeline pipeline;
    char parse_error[256];
    if (shell_parse_line(line, &pipeline, parse_error, sizeof(parse_error)) != 0) {
        dprintf(err_fd, "parse: %s\n", parse_error);
        return 2;
    }
    if (pipeline.count == 0) {
        shell_pipeline_free(&pipeline);
        return 0;
    }
    if (pipeline.background) {
        dprintf(err_fd, "remote/noninteractive execution does not allow background '&'\n");
        shell_pipeline_free(&pipeline);
        return 2;
    }
    static const char *forbidden[] = {"cd", "exit", "quit", "history", "jobs", "fg", "bg", NULL};
    for (size_t i = 0; forbidden[i]; ++i) {
        if (strcmp(pipeline.commands[0].argv[0], forbidden[i]) == 0 && pipeline.count == 1) {
            dprintf(err_fd, "%s: stateful shell builtin is unavailable in noninteractive mode\n", forbidden[i]);
            shell_pipeline_free(&pipeline);
            return 2;
        }
    }
    ShellState dummy;
    memset(&dummy, 0, sizeof(dummy));
    int rc = execute_external_pipeline(&dummy, &pipeline, out_fd, err_fd, 0);
    shell_pipeline_free(&pipeline);
    return rc;
}
