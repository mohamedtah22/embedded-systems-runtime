#include "embedded_protocol.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

uint32_t mlrt_crc32_begin(void) {
    return 0xFFFFFFFFu;
}

uint32_t mlrt_crc32_update(uint32_t state, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        state ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(state & 1u);
            state = (state >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return state;
}

uint32_t mlrt_crc32_end(uint32_t state) {
    return state ^ 0xFFFFFFFFu;
}

uint32_t mlrt_crc32(const void *data, size_t len) {
    return mlrt_crc32_end(mlrt_crc32_update(mlrt_crc32_begin(), data, len));
}

int mlrt_packet_encode(const mlrt_packet *packet, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!packet || !out || !out_len) return MLRT_PROTO_EINVAL;
    if (packet->length > MLRT_PROTO_MAX_PAYLOAD) return MLRT_PROTO_ETOOBIG;

    size_t total = MLRT_PROTO_HEADER_SIZE + packet->length + MLRT_PROTO_CRC_SIZE;
    if (out_cap < total) return MLRT_PROTO_ETOOBIG;

    put_u16_be(out, MLRT_PROTO_MAGIC);
    out[2] = packet->version ? packet->version : MLRT_PROTO_VERSION;
    out[3] = packet->type;
    put_u16_be(out + 4, packet->length);
    put_u32_be(out + 6, packet->seq);
    if (packet->length) memcpy(out + MLRT_PROTO_HEADER_SIZE, packet->payload, packet->length);

    uint32_t crc = mlrt_crc32(out, MLRT_PROTO_HEADER_SIZE + packet->length);
    put_u32_be(out + MLRT_PROTO_HEADER_SIZE + packet->length, crc);
    *out_len = total;
    return MLRT_PROTO_OK;
}

int mlrt_packet_decode(const uint8_t *frame, size_t frame_len, mlrt_packet *packet) {
    if (!frame || !packet || frame_len < MLRT_PROTO_HEADER_SIZE + MLRT_PROTO_CRC_SIZE) return MLRT_PROTO_EINVAL;
    if (get_u16_be(frame) != MLRT_PROTO_MAGIC) return MLRT_PROTO_EFORMAT;
    if (frame[2] != MLRT_PROTO_VERSION) return MLRT_PROTO_EFORMAT;

    uint16_t payload_len = get_u16_be(frame + 4);
    if (payload_len > MLRT_PROTO_MAX_PAYLOAD) return MLRT_PROTO_ETOOBIG;
    size_t expected = MLRT_PROTO_HEADER_SIZE + payload_len + MLRT_PROTO_CRC_SIZE;
    if (frame_len != expected) return MLRT_PROTO_EFORMAT;

    uint32_t expected_crc = get_u32_be(frame + MLRT_PROTO_HEADER_SIZE + payload_len);
    uint32_t actual_crc = mlrt_crc32(frame, MLRT_PROTO_HEADER_SIZE + payload_len);
    if (expected_crc != actual_crc) return MLRT_PROTO_ECRC;

    memset(packet, 0, sizeof(*packet));
    packet->version = frame[2];
    packet->type = frame[3];
    packet->length = payload_len;
    packet->seq = get_u32_be(frame + 6);
    if (payload_len) memcpy(packet->payload, frame + MLRT_PROTO_HEADER_SIZE, payload_len);
    return MLRT_PROTO_OK;
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return MLRT_PROTO_EIO;
        }
        if (n == 0) return MLRT_PROTO_EIO;
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return MLRT_PROTO_OK;
}

static int read_all(int fd, uint8_t *buf, size_t len) {
    while (len) {
        ssize_t n = read(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return MLRT_PROTO_EIO;
        }
        if (n == 0) return MLRT_PROTO_EOF;
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return MLRT_PROTO_OK;
}

int mlrt_packet_write_fd(int fd, const mlrt_packet *packet) {
    uint8_t frame[MLRT_PROTO_MAX_FRAME];
    size_t frame_len = 0;
    int rc = mlrt_packet_encode(packet, frame, sizeof(frame), &frame_len);
    if (rc != MLRT_PROTO_OK) return rc;
    return write_all(fd, frame, frame_len);
}

int mlrt_packet_read_fd(int fd, mlrt_packet *packet) {
    uint8_t frame[MLRT_PROTO_MAX_FRAME];
    int rc = read_all(fd, frame, MLRT_PROTO_HEADER_SIZE);
    if (rc != MLRT_PROTO_OK) return rc;
    if (get_u16_be(frame) != MLRT_PROTO_MAGIC || frame[2] != MLRT_PROTO_VERSION) return MLRT_PROTO_EFORMAT;

    uint16_t payload_len = get_u16_be(frame + 4);
    if (payload_len > MLRT_PROTO_MAX_PAYLOAD) return MLRT_PROTO_ETOOBIG;
    size_t tail = (size_t)payload_len + MLRT_PROTO_CRC_SIZE;
    rc = read_all(fd, frame + MLRT_PROTO_HEADER_SIZE, tail);
    if (rc != MLRT_PROTO_OK) return rc;
    return mlrt_packet_decode(frame, MLRT_PROTO_HEADER_SIZE + tail, packet);
}

const char *mlrt_message_type_name(uint8_t type) {
    switch (type) {
        case MLRT_MSG_HEARTBEAT: return "HEARTBEAT";
        case MLRT_MSG_SENSOR_DATA: return "SENSOR_DATA";
        case MLRT_MSG_STATUS_REQ: return "STATUS_REQ";
        case MLRT_MSG_STATUS_RSP: return "STATUS_RSP";
        case MLRT_MSG_CONFIG_SET: return "CONFIG_SET";
        case MLRT_MSG_CONFIG_ACK: return "CONFIG_ACK";
        case MLRT_MSG_ERROR: return "ERROR";
        case MLRT_MSG_FW_INFO_REQ: return "FW_INFO_REQ";
        case MLRT_MSG_FW_INFO_RSP: return "FW_INFO_RSP";
        case MLRT_MSG_FAULT_INJECT: return "FAULT_INJECT";
        case MLRT_MSG_RESET: return "RESET";
        case MLRT_MSG_RESET_ACK: return "RESET_ACK";
        default: return "UNKNOWN";
    }
}
