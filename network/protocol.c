#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len) {
    unsigned char *p = buf;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

int net_send_frame(int fd, const void *data, uint32_t length) {
    uint32_t net_length = htonl(length);
    if (send_all(fd, &net_length, sizeof(net_length)) < 0) return -1;
    return length ? send_all(fd, data, length) : 0;
}

int net_recv_frame(int fd, char **data, uint32_t *length, uint32_t max_length) {
    uint32_t net_length;
    int rc = recv_all(fd, &net_length, sizeof(net_length));
    if (rc <= 0) return rc;
    uint32_t host_length = ntohl(net_length);
    if (host_length > max_length) {
        errno = EMSGSIZE;
        return -1;
    }
    char *buf = malloc((size_t)host_length + 1);
    if (!buf) return -1;
    if (host_length) {
        rc = recv_all(fd, buf, host_length);
        if (rc <= 0) { free(buf); return rc; }
    }
    buf[host_length] = '\0';
    *data = buf;
    *length = host_length;
    return 1;
}
