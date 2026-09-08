#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "shell.h"
#include "mlrt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t stop_server = 0;
static pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t client_cv = PTHREAD_COND_INITIALIZER;
static unsigned active_clients = 0;

typedef struct {
    int fd;
} ClientCtx;

static void on_signal(int signo) {
    (void)signo;
    stop_server = 1;
}

static int execute_capture(const char *command, char **output, size_t *output_len, int *exit_code) {
    int p[2];
    if (pipe(p) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        close(p[0]);
        int rc = shell_run_noninteractive(command, p[1], p[1]);
        close(p[1]);
        _exit(rc & 0xff);
    }
    close(p[1]);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(p[0]); kill(pid, SIGTERM); waitpid(pid, NULL, 0); return -1; }
    for (;;) {
        char chunk[4096];
        ssize_t n = read(p[0], chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf); close(p[0]); waitpid(pid, NULL, 0); return -1;
        }
        if (n == 0) break;
        if (len + (size_t)n > MLRT_MAX_RESPONSE) {
            const char truncated[] = "\n[output truncated at 1 MiB]\n";
            size_t room = MLRT_MAX_RESPONSE > len ? MLRT_MAX_RESPONSE - len : 0;
            if (room) memcpy(buf + len, chunk, room), len += room;
            size_t tlen = sizeof(truncated) - 1;
            if (len + tlen <= cap) memcpy(buf + len, truncated, tlen), len += tlen;
            kill(pid, SIGTERM);
            break;
        }
        if (len + (size_t)n + 1 > cap) {
            size_t new_cap = cap * 2;
            while (new_cap < len + (size_t)n + 1) new_cap *= 2;
            char *next = realloc(buf, new_cap);
            if (!next) { free(buf); close(p[0]); kill(pid, SIGTERM); waitpid(pid, NULL, 0); return -1; }
            buf = next; cap = new_cap;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
    }
    close(p[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
    else *exit_code = 1;
    if (len + 1 > cap) {
        char *next = realloc(buf, len + 1);
        if (!next) { free(buf); return -1; }
        buf = next;
    }
    buf[len] = '\0';
    *output = buf;
    *output_len = len;
    return 0;
}

static void *client_thread(void *arg) {
    ClientCtx *ctx = arg;
    int fd = ctx->fd;
    free(ctx);

    for (;;) {
        char *command = NULL;
        uint32_t command_len = 0;
        int rc = net_recv_frame(fd, &command, &command_len, MLRT_MAX_COMMAND);
        if (rc <= 0) { free(command); break; }
        if (command_len == 0 || strcmp(command, "exit") == 0) { free(command); break; }

        char *output = NULL;
        size_t output_len = 0;
        int code = 0;
        if (execute_capture(command, &output, &output_len, &code) < 0) {
            const char *msg = "exit_status=1\nserver: command execution failed\n";
            net_send_frame(fd, msg, (uint32_t)strlen(msg));
        } else {
            char header[64];
            int hlen = snprintf(header, sizeof(header), "exit_status=%d\n", code);
            size_t total = (size_t)hlen + output_len;
            char *response = malloc(total + 1);
            if (!response) {
                free(output); free(command); break;
            }
            memcpy(response, header, (size_t)hlen);
            memcpy(response + hlen, output, output_len);
            response[total] = '\0';
            if (net_send_frame(fd, response, (uint32_t)total) < 0) {
                free(response); free(output); free(command); break;
            }
            free(response);
            free(output);
        }
        free(command);
    }
    close(fd);
    pthread_mutex_lock(&client_lock);
    if (active_clients) active_clients--;
    pthread_cond_broadcast(&client_cv);
    pthread_mutex_unlock(&client_lock);
    return NULL;
}

static int parse_port(const char *text) {
    char *end = NULL;
    long p = strtol(text, &end, 10);
    return end && *end == '\0' && p > 0 && p <= 65535 ? (int)p : -1;
}

int network_server_cli_main(int argc, char **argv) {
    int port = 5050;
    int once = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
            if (port < 0) { fprintf(stderr, "invalid port\n"); return 2; }
        } else if (strcmp(argv[i], "--once") == 0) once = 1;
        else {
            fprintf(stderr, "usage: mlrt server [--port N] [--once]\n");
            return 2;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(listener); return 1; }
    if (listen(listener, 16) < 0) { perror("listen"); close(listener); return 1; }
    printf("mlrt local remote-shell server listening on 127.0.0.1:%d\n", port);
    fflush(stdout);

    unsigned accepted = 0;
    while (!stop_server) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        ClientCtx *ctx = malloc(sizeof(*ctx));
        if (!ctx) { close(client); continue; }
        ctx->fd = client;
        pthread_t tid;
        pthread_mutex_lock(&client_lock);
        active_clients++;
        pthread_mutex_unlock(&client_lock);
        if (pthread_create(&tid, NULL, client_thread, ctx) != 0) {
            perror("pthread_create");
            close(client); free(ctx);
            pthread_mutex_lock(&client_lock); active_clients--; pthread_mutex_unlock(&client_lock);
            continue;
        }
        pthread_detach(tid);
        accepted++;
        if (once && accepted >= 1) break;
    }
    close(listener);

    pthread_mutex_lock(&client_lock);
    while (active_clients) pthread_cond_wait(&client_cv, &client_lock);
    pthread_mutex_unlock(&client_lock);
    return 0;
}
