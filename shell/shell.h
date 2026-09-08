#ifndef MLRT_SHELL_H
#define MLRT_SHELL_H

#include "jobs.h"

#include <stddef.h>
#include <sys/types.h>

#define SHELL_HISTORY_MAX 100

typedef struct {
    JobList jobs;
    char *history[SHELL_HISTORY_MAX];
    size_t history_count;
    unsigned long history_base;
    int interactive;
    int terminal_fd;
    pid_t shell_pgid;
    int should_exit;
} ShellState;

int shell_state_init(ShellState *state, int interactive);
void shell_state_destroy(ShellState *state);
int shell_run_line(ShellState *state, const char *line, int out_fd, int err_fd);
int shell_run_noninteractive(const char *line, int out_fd, int err_fd);

#endif
