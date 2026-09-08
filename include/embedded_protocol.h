#ifndef MLRT_EMBEDDED_PROTOCOL_H
#define MLRT_EMBEDDED_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define MLRT_PROTO_MAGIC 0xA55Au
#define MLRT_PROTO_VERSION 1u
#define MLRT_PROTO_MAX_PAYLOAD 512u
#define MLRT_PROTO_HEADER_SIZE 10u
#define MLRT_PROTO_CRC_SIZE 4u
#define MLRT_PROTO_MAX_FRAME (MLRT_PROTO_HEADER_SIZE + MLRT_PROTO_MAX_PAYLOAD + MLRT_PROTO_CRC_SIZE)

typedef enum {
    MLRT_MSG_HEARTBEAT = 1,
    MLRT_MSG_SENSOR_DATA = 2,
    MLRT_MSG_STATUS_REQ = 3,
    MLRT_MSG_STATUS_RSP = 4,
    MLRT_MSG_CONFIG_SET = 5,
    MLRT_MSG_CONFIG_ACK = 6,
    MLRT_MSG_ERROR = 7,
    MLRT_MSG_FW_INFO_REQ = 8,
    MLRT_MSG_FW_INFO_RSP = 9,
    MLRT_MSG_FAULT_INJECT = 10,
    MLRT_MSG_RESET = 11,
    MLRT_MSG_RESET_ACK = 12
} mlrt_message_type;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t length;
    uint32_t seq;
    uint8_t payload[MLRT_PROTO_MAX_PAYLOAD];
} mlrt_packet;

typedef enum {
    MLRT_PROTO_OK = 0,
    MLRT_PROTO_EINVAL = -1,
    MLRT_PROTO_ETOOBIG = -2,
    MLRT_PROTO_EFORMAT = -3,
    MLRT_PROTO_ECRC = -4,
    MLRT_PROTO_EIO = -5,
    MLRT_PROTO_EOF = -6
} mlrt_proto_status;

uint32_t mlrt_crc32_begin(void);
uint32_t mlrt_crc32_update(uint32_t state, const void *data, size_t len);
uint32_t mlrt_crc32_end(uint32_t state);
uint32_t mlrt_crc32(const void *data, size_t len);

int mlrt_packet_encode(const mlrt_packet *packet, uint8_t *out, size_t out_cap, size_t *out_len);
int mlrt_packet_decode(const uint8_t *frame, size_t frame_len, mlrt_packet *packet);
int mlrt_packet_write_fd(int fd, const mlrt_packet *packet);
int mlrt_packet_read_fd(int fd, mlrt_packet *packet);

const char *mlrt_message_type_name(uint8_t type);

#endif
