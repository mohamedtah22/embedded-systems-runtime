#include "mlrt.h"
#include "embedded_protocol.h"
#include "virtual_uart.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int set_raw(int fd) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    return tcsetattr(fd, TCSANOW, &tio);
}

int mlrt_virtual_uart_open(mlrt_virtual_uart *uart) {
    if (!uart) return -1;
    memset(uart, 0, sizeof(*uart));
    uart->master_fd = -1;
    uart->slave_fd = -1;

    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) return -1;
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        close(master);
        return -1;
    }
    char path[128];
    if (ptsname_r(master, path, sizeof(path)) != 0) {
        close(master);
        return -1;
    }
    int slave = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        close(master);
        return -1;
    }
    if (set_raw(master) != 0 || set_raw(slave) != 0) {
        close(slave);
        close(master);
        return -1;
    }

    uart->master_fd = master;
    uart->slave_fd = slave;
    snprintf(uart->slave_path, sizeof(uart->slave_path), "%s", path);
    return 0;
}

void mlrt_virtual_uart_close(mlrt_virtual_uart *uart) {
    if (!uart) return;
    if (uart->master_fd >= 0) close(uart->master_fd);
    if (uart->slave_fd >= 0) close(uart->slave_fd);
    uart->master_fd = uart->slave_fd = -1;
}

static int child_target(int fd) {
    mlrt_packet req;
    int rc = mlrt_packet_read_fd(fd, &req);
    if (rc != MLRT_PROTO_OK || req.type != MLRT_MSG_STATUS_REQ) return 1;

    mlrt_packet rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.version = MLRT_PROTO_VERSION;
    rsp.type = MLRT_MSG_STATUS_RSP;
    rsp.seq = req.seq;
    const char text[] = "state=RUNNING rate_hz=100 transport=PTY";
    rsp.length = (uint16_t)strlen(text);
    memcpy(rsp.payload, text, rsp.length);
    return mlrt_packet_write_fd(fd, &rsp) == MLRT_PROTO_OK ? 0 : 1;
}

int serial_demo_cli_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    mlrt_virtual_uart uart;
    if (mlrt_virtual_uart_open(&uart) != 0) {
        perror("mlrt serial-demo: posix_openpt");
        return 1;
    }

    printf("Virtual UART created\n");
    printf("  slave device: %s\n", uart.slave_path);
    printf("  transport: PTY (pseudo-terminal)\n");

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mlrt_virtual_uart_close(&uart);
        return 1;
    }
    if (pid == 0) {
        close(uart.master_fd);
        int rc = child_target(uart.slave_fd);
        close(uart.slave_fd);
        _exit(rc);
    }

    close(uart.slave_fd);
    uart.slave_fd = -1;

    mlrt_packet req;
    memset(&req, 0, sizeof(req));
    req.version = MLRT_PROTO_VERSION;
    req.type = MLRT_MSG_STATUS_REQ;
    req.seq = 1;
    if (mlrt_packet_write_fd(uart.master_fd, &req) != MLRT_PROTO_OK) {
        fprintf(stderr, "mlrt serial-demo: failed to send request\n");
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        mlrt_virtual_uart_close(&uart);
        return 1;
    }

    mlrt_packet rsp;
    int rc = mlrt_packet_read_fd(uart.master_fd, &rsp);
    int status = 0;
    waitpid(pid, &status, 0);
    if (rc != MLRT_PROTO_OK || rsp.type != MLRT_MSG_STATUS_RSP || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "mlrt serial-demo: protocol exchange failed\n");
        mlrt_virtual_uart_close(&uart);
        return 1;
    }

    printf("Host -> target: %s seq=%u\n", mlrt_message_type_name(req.type), req.seq);
    printf("Target -> host: %s seq=%u payload=\"%.*s\"\n",
           mlrt_message_type_name(rsp.type), rsp.seq, rsp.length, (char *)rsp.payload);
    printf("CRC/framing check: OK\n");
    mlrt_virtual_uart_close(&uart);
    return 0;
}
