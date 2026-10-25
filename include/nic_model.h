#ifndef MLRT_NIC_MODEL_H
#define MLRT_NIC_MODEL_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#define NIC_MAX_RING 64u
#define NIC_MTU 1536u
#define NIC_IRQ_TX 1u
#define NIC_IRQ_RX 2u
#define NIC_IRQ_ERROR 4u
enum nic_owner { NIC_FREE, NIC_DEVICE, NIC_DONE };
typedef struct {
    atomic_uint owner;
    size_t length;
    unsigned error;
    uint8_t buffer[NIC_MTU];
} nic_descriptor;
typedef struct {
    nic_descriptor tx[NIC_MAX_RING], rx[NIC_MAX_RING];
    unsigned capacity;
    unsigned tx_submit_cursor, tx_reclaim_cursor, device_tx_cursor;
    unsigned device_rx_cursor, rx_consume_cursor;
    atomic_uint tx_head, tx_tail, rx_head, rx_tail, irq_status, inject_fault;
    atomic_uint_fast64_t tx_completed, rx_completed, rx_no_buffer, tx_errors;
} nic_model;
/* Software device model. No real MMIO, physical DMA, or hardware interrupts.
 * One driver thread owns submit/reclaim/receive; one device thread owns step.
 * Initialization/reset requires both threads quiescent. */
int nic_model_init(nic_model *nic, unsigned capacity);
int nic_tx_submit(nic_model *nic, const void *packet, size_t length); /* 1=full */
unsigned nic_device_step(nic_model *nic, unsigned budget);
unsigned nic_tx_reclaim(nic_model *nic, unsigned *errors);
int nic_rx_receive(nic_model *nic, void *packet, size_t capacity, size_t *length);
unsigned nic_take_interrupts(nic_model *nic);
void nic_inject_tx_error(nic_model *nic);
#endif
