#ifndef MLRT_VIRTUAL_UART_H
#define MLRT_VIRTUAL_UART_H

#include <stddef.h>

typedef struct {
    int master_fd;
    int slave_fd;
    char slave_path[128];
} mlrt_virtual_uart;

int mlrt_virtual_uart_open(mlrt_virtual_uart *uart);
void mlrt_virtual_uart_close(mlrt_virtual_uart *uart);

#endif
