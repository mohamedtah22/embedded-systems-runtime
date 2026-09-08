#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "mlrt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_port(const char *text) {
    char *end = NULL;
    long p = strtol(text, &end, 10);
    return end && *end == '\0' && p > 0 && p <= 65535 ? (int)p : -1;
}

static int send_command(int fd, const char *command) {
    size_t len = strlen(command);
    if (len > MLRT_MAX_COMMAND) { fprintf(stderr, "command too long\n"); return 2; }
    if (net_send_frame(fd, command, (uint32_t)len) < 0) { perror("send"); return 1; }
    char *response = NULL;
    uint32_t response_len = 0;
    int rc = net_recv_frame(fd, &response, &response_len, MLRT_MAX_RESPONSE + 128);
    if (rc <= 0) { if (rc < 0) perror("recv"); free(response); return 1; }
    fwrite(response, 1, response_len, stdout);
    if (response_len && response[response_len - 1] != '\n') putchar('\n');
    free(response);
    return 0;
}

int network_client_cli_main(int argc, char **argv) {
    int port = 5050;
    const char *command = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
            if (port < 0) { fprintf(stderr, "invalid port\n"); return 2; }
        } else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) && i + 1 < argc) {
            command = argv[++i];
        } else {
            fprintf(stderr, "usage: mlrt client [--port N] [-c COMMAND]\n");
            return 2;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); close(fd); return 1; }

    int rc = 0;
    if (command) {
        rc = send_command(fd, command);
    } else {
        char *line = NULL;
        size_t cap = 0;
        while (1) {
            printf("remote> "); fflush(stdout);
            ssize_t n = getline(&line, &cap, stdin);
            if (n < 0) break;
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
            if (strcmp(line, "exit") == 0) break;
            rc = send_command(fd, line);
            if (rc) break;
        }
        free(line);
    }
    net_send_frame(fd, "exit", 4);
    close(fd);
    return rc;
}
