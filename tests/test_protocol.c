#include "embedded_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    const char *text = "123456789";
    assert(mlrt_crc32(text, strlen(text)) == 0xcbf43926u);

    mlrt_packet p;
    memset(&p, 0, sizeof(p));
    p.version = MLRT_PROTO_VERSION;
    p.type = MLRT_MSG_CONFIG_SET;
    p.seq = 42;
    p.length = 3;
    p.payload[0] = 1; p.payload[1] = 2; p.payload[2] = 3;

    uint8_t frame[MLRT_PROTO_MAX_FRAME];
    size_t len = 0;
    assert(mlrt_packet_encode(&p, frame, sizeof(frame), &len) == MLRT_PROTO_OK);
    mlrt_packet out;
    assert(mlrt_packet_decode(frame, len, &out) == MLRT_PROTO_OK);
    assert(out.type == p.type && out.seq == 42 && out.length == 3 && out.payload[2] == 3);

    frame[len - 1] ^= 1;
    assert(mlrt_packet_decode(frame, len, &out) == MLRT_PROTO_ECRC);

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(mlrt_packet_write_fd(sv[0], &p) == MLRT_PROTO_OK);
    assert(mlrt_packet_read_fd(sv[1], &out) == MLRT_PROTO_OK);
    assert(out.seq == 42);
    close(sv[0]); close(sv[1]);

    puts("test_protocol: PASS");
    return 0;
}
