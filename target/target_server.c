#include "target_sim.h"
#include "logger.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/tmp/mlrt-target.sock"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int serve_client(mlrt_target_ctx *ctx, int fd) {
    for (;;) {
        mlrt_packet req;
        int rc = mlrt_packet_read_fd(fd, &req);
        if (rc == MLRT_PROTO_EOF) return 0;
        if (rc == MLRT_PROTO_ECRC) {
            atomic_fetch_add(&ctx->crc_errors, 1);
            mlrt_log(MLRT_LOG_WARN, "PROTO", "rejected frame with invalid CRC");
            return 0;
        }
        if (rc != MLRT_PROTO_OK) return -1;

        mlrt_packet rsp;
        if (mlrt_target_handle_packet(ctx, &req, &rsp) != 0) return -1;
        if (mlrt_packet_write_fd(fd, &rsp) != MLRT_PROTO_OK) return -1;
    }
}

static int run_server(const char *path, int quiet) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "mlrt target-sim: socket path too long\n");
        close(fd);
        return 2;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind"); close(fd); return 1;
    }
    if (listen(fd, 8) != 0) {
        perror("listen"); close(fd); unlink(path); return 1;
    }

    mlrt_target_ctx ctx;
    if (mlrt_target_init(&ctx, 100) != 0 || mlrt_target_start(&ctx) != 0) {
        fprintf(stderr, "mlrt target-sim: failed to start target runtime\n");
        close(fd); unlink(path); return 1;
    }

    if (!quiet) {
        printf("Embedded target simulator running\n");
        printf("  socket: %s\n", path);
        printf("  state:  %s\n", mlrt_target_state_name(atomic_load(&ctx.state)));
        fflush(stdout);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (!g_stop) {
        int client = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        (void)serve_client(&ctx, client);
        close(client);
    }

    mlrt_target_stop(&ctx);
    close(fd);
    unlink(path);
    return 0;
}

int target_server_self_test(void) {
    mlrt_target_ctx ctx;
    if (mlrt_target_init(&ctx, 100) != 0) return 1;
    if (mlrt_target_start(&ctx) != 0) { mlrt_target_stop(&ctx); return 1; }
    usleep(50000);
    mlrt_packet req = {.version = MLRT_PROTO_VERSION, .type = MLRT_MSG_STATUS_REQ, .seq = 7};
    mlrt_packet rsp;
    int rc = mlrt_target_handle_packet(&ctx, &req, &rsp);
    int ok = rc == 0 && rsp.type == MLRT_MSG_STATUS_RSP && rsp.length == 27;
#if defined(__aarch64__)
    const char *arch = "arm64";
#elif defined(__x86_64__)
    const char *arch = "x86_64";
#else
    const char *arch = "other";
#endif
    printf("target-self-test: arch=%s state=%s protocol=%s\n",
           arch, mlrt_target_state_name(atomic_load(&ctx.state)), ok ? "PASS" : "FAIL");
    mlrt_target_stop(&ctx);
    return ok ? 0 : 1;
}

static void server_usage(FILE *out) {
    fprintf(out, "usage: mlrt target-sim [--socket PATH] [--quiet]\n");
}

int target_server_cli_main(int argc, char **argv) {
    const char *path = DEFAULT_SOCKET;
    int quiet = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) path = argv[++i];
        else if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
        else if (strcmp(argv[i], "--self-test") == 0) return target_server_self_test();
        else if (strcmp(argv[i], "--help") == 0) { server_usage(stdout); return 0; }
        else { server_usage(stderr); return 2; }
    }
    return run_server(path, quiet);
}
