#define _POSIX_C_SOURCE 200809L
#include "shell.h"
#include "mlrt.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int run_interactive(void) {
    ShellState state;
    if (shell_state_init(&state, isatty(STDIN_FILENO)) != 0) return 1;

    char *line = NULL;
    size_t cap = 0;
    while (!state.should_exit) {
        jobs_reap(&state.jobs, STDOUT_FILENO);
        if (state.interactive) {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) printf("mlrt:%s$ ", cwd);
            else printf("mlrt$ ");
            fflush(stdout);
        }
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (errno == EINTR) { clearerr(stdin); continue; }
            break;
        }
        shell_run_line(&state, line, STDOUT_FILENO, STDERR_FILENO);
    }
    free(line);
    shell_state_destroy(&state);
    return 0;
}

int shell_cli_main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: mlrt shell -c 'command'\n");
            return 2;
        }
        return shell_run_noninteractive(argv[2], STDOUT_FILENO, STDERR_FILENO);
    }
    if (argc >= 2 && strcmp(argv[1], "--script") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: mlrt shell --script FILE\n");
            return 2;
        }
        FILE *fp = fopen(argv[2], "r");
        if (!fp) { perror(argv[2]); return 1; }
        ShellState state;
        shell_state_init(&state, 0);
        char *line = NULL;
        size_t cap = 0;
        int rc = 0;
        while (getline(&line, &cap, fp) >= 0 && !state.should_exit) {
            rc = shell_run_line(&state, line, STDOUT_FILENO, STDERR_FILENO);
        }
        free(line);
        shell_state_destroy(&state);
        fclose(fp);
        return rc;
    }
    return run_interactive();
}
