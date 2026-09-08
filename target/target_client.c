#include "target_sim.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/tmp/mlrt-target.sock"

static uint16_t get_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static int connect_target(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) { close(fd); errno = ENAMETOOLONG; return -1; }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    return fd;
}

static int transact(const char *path, mlrt_packet *req, mlrt_packet *rsp) {
    int fd = connect_target(path);
    if (fd < 0) { perror("mlrt target: connect"); return 1; }
    int rc = mlrt_packet_write_fd(fd, req);
    if (rc == MLRT_PROTO_OK) rc = mlrt_packet_read_fd(fd, rsp);
    close(fd);
    if (rc != MLRT_PROTO_OK) {
        fprintf(stderr, "mlrt target: protocol error %d\n", rc);
        return 1;
    }
    if (rsp->type == MLRT_MSG_ERROR) {
        fprintf(stderr, "target error: %.*s\n", rsp->length, (char *)rsp->payload);
        return 1;
    }
    return 0;
}

static void print_status(const mlrt_packet *rsp) {
    if (rsp->type != MLRT_MSG_STATUS_RSP || rsp->length != 27) {
        fprintf(stderr, "mlrt target: malformed STATUS_RSP\n");
        return;
    }
    int state = rsp->payload[0];
    unsigned rate = get_u16_be(rsp->payload + 1);
    uint32_t uptime = get_u32_be(rsp->payload + 3);
    uint32_t produced = get_u32_be(rsp->payload + 7);
    uint32_t processed = get_u32_be(rsp->payload + 11);
    uint32_t dropped = get_u32_be(rsp->payload + 15);
    uint32_t crc_errors = get_u32_be(rsp->payload + 19);
    uint32_t wd = get_u32_be(rsp->payload + 23);
    printf("Target status\n");
    printf("  state:               %s\n", mlrt_target_state_name(state));
    printf("  rate_hz:             %u\n", rate);
    printf("  uptime_ms:           %u\n", uptime);
    printf("  produced_samples:    %u\n", produced);
    printf("  processed_samples:   %u\n", processed);
    printf("  dropped_samples:     %u\n", dropped);
    printf("  crc_errors:          %u\n", crc_errors);
    printf("  watchdog_recoveries: %u\n", wd);
}

static int send_corrupt_crc(const char *path) {
    int fd = connect_target(path);
    if (fd < 0) { perror("mlrt target: connect"); return 1; }
    mlrt_packet req;
    memset(&req, 0, sizeof(req));
    req.version = MLRT_PROTO_VERSION;
    req.type = MLRT_MSG_STATUS_REQ;
    req.seq = 0xBADu;
    uint8_t frame[MLRT_PROTO_MAX_FRAME];
    size_t frame_len = 0;
    if (mlrt_packet_encode(&req, frame, sizeof(frame), &frame_len) != MLRT_PROTO_OK) { close(fd); return 1; }
    frame[frame_len - 1] ^= 0x01u;
    size_t off = 0;
    while (off < frame_len) {
        ssize_t n = write(fd, frame + off, frame_len - off);
        if (n < 0) { if (errno == EINTR) continue; perror("write"); close(fd); return 1; }
        if (n == 0) { close(fd); return 1; }
        off += (size_t)n;
    }
    shutdown(fd, SHUT_WR);
    close(fd);
    printf("Injected protocol fault: corrupted CRC frame sent\n");
    return 0;
}

static void client_usage(FILE *out) {
    fprintf(out,
        "usage: mlrt target <command> [args] [--socket PATH]\n"
        "commands:\n"
        "  status\n"
        "  set-rate HZ\n"
        "  inject-fault freeze-worker|corrupt-crc\n"
        "  reset\n"
        "  fw-info\n");
}

int target_client_cli_main(int argc, char **argv) {
    if (argc < 2) { client_usage(stderr); return 2; }
    const char *command = argv[1];
    const char *path = DEFAULT_SOCKET;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) { path = argv[++i]; continue; }
    }

    mlrt_packet req;
    memset(&req, 0, sizeof(req));
    req.version = MLRT_PROTO_VERSION;
    req.seq = 1;

    if (strcmp(command, "status") == 0) {
        req.type = MLRT_MSG_STATUS_REQ;
    } else if (strcmp(command, "set-rate") == 0) {
        if (argc < 3) { client_usage(stderr); return 2; }
        char *end = NULL;
        long rate = strtol(argv[2], &end, 10);
        if (!end || *end || rate < 1 || rate > 1000) { fprintf(stderr, "rate must be 1..1000\n"); return 2; }
        req.type = MLRT_MSG_CONFIG_SET;
        req.length = 2;
        put_u16_be(req.payload, (uint16_t)rate);
    } else if (strcmp(command, "inject-fault") == 0) {
        if (argc < 3) { client_usage(stderr); return 2; }
        if (strcmp(argv[2], "corrupt-crc") == 0) return send_corrupt_crc(path);
        if (strcmp(argv[2], "freeze-worker") != 0) { client_usage(stderr); return 2; }
        req.type = MLRT_MSG_FAULT_INJECT;
        req.length = 1;
        req.payload[0] = 1;
    } else if (strcmp(command, "reset") == 0) {
        req.type = MLRT_MSG_RESET;
    } else if (strcmp(command, "fw-info") == 0) {
        req.type = MLRT_MSG_FW_INFO_REQ;
    } else if (strcmp(command, "--help") == 0 || strcmp(command, "help") == 0) {
        client_usage(stdout); return 0;
    } else {
        client_usage(stderr); return 2;
    }

    mlrt_packet rsp;
    if (transact(path, &req, &rsp) != 0) return 1;
    if (rsp.type == MLRT_MSG_STATUS_RSP) {
        print_status(&rsp);
    } else if (rsp.type == MLRT_MSG_CONFIG_ACK) {
        if (req.type == MLRT_MSG_CONFIG_SET && rsp.length == 2)
            printf("Target acknowledged rate_hz=%u\n", get_u16_be(rsp.payload));
        else
            printf("Target accepted fault injection: freeze-worker\n");
    } else if (rsp.type == MLRT_MSG_RESET_ACK) {
        printf("Target reset acknowledged\n");
    } else if (rsp.type == MLRT_MSG_FW_INFO_RSP) {
        printf("Firmware info: %.*s\n", rsp.length, (char *)rsp.payload);
    } else {
        printf("Target response: %s\n", mlrt_message_type_name(rsp.type));
    }
    return 0;
}
