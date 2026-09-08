#ifndef MLRT_NETWORK_PROTOCOL_H
#define MLRT_NETWORK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define MLRT_MAX_COMMAND 4096U
#define MLRT_MAX_RESPONSE (1024U * 1024U)

int net_send_frame(int fd, const void *data, uint32_t length);
int net_recv_frame(int fd, char **data, uint32_t *length, uint32_t max_length);

#endif
