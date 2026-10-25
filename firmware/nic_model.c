#include "nic_model.h"
#include <string.h>

int nic_model_init(nic_model *n, unsigned capacity) {
    if (!n || capacity == 0 || capacity > NIC_MAX_RING) return -1;
    memset(n, 0, sizeof(*n));
    n->capacity = capacity;
    for (unsigned i=0; i<NIC_MAX_RING; ++i) {
        atomic_init(&n->tx[i].owner, NIC_FREE);
        atomic_init(&n->rx[i].owner, NIC_DEVICE);
    }
    atomic_init(&n->tx_head,0); atomic_init(&n->tx_tail,0);
    atomic_init(&n->rx_head,0); atomic_init(&n->rx_tail,0);
    atomic_init(&n->irq_status,0); atomic_init(&n->inject_fault,0);
    atomic_init(&n->tx_completed,0); atomic_init(&n->rx_completed,0);
    atomic_init(&n->rx_no_buffer,0); atomic_init(&n->tx_errors,0);
    return 0;
}
int nic_tx_submit(nic_model *n, const void *packet, size_t length) {
    if (!n || !packet || length == 0 || length > NIC_MTU) return -1;
    nic_descriptor *d = &n->tx[n->tx_submit_cursor];
    if (atomic_load_explicit(&d->owner,memory_order_acquire) != NIC_FREE) return 1;
    memcpy(d->buffer,packet,length);
    d->length=length; d->error=0;
    atomic_store_explicit(&d->owner,NIC_DEVICE,memory_order_release);
    n->tx_submit_cursor=(n->tx_submit_cursor+1)%n->capacity;
    atomic_store(&n->tx_tail,n->tx_submit_cursor);
    return 0;
}
unsigned nic_device_step(nic_model *n, unsigned budget) {
    if (!n) return 0;
    // Bound each poll even when the driver concurrently refills the ring.
    if (budget > n->capacity) budget=n->capacity;
    unsigned done=0;
    while (done < budget) {
        nic_descriptor *tx=&n->tx[n->device_tx_cursor];
        if (atomic_load_explicit(&tx->owner,memory_order_acquire) != NIC_DEVICE) break;
        unsigned error=atomic_exchange(&n->inject_fault,0);
        if (error || tx->length == 0 || tx->length > NIC_MTU) {
            tx->error=1;
            atomic_fetch_add(&n->tx_errors,1);
            atomic_fetch_or(&n->irq_status,NIC_IRQ_ERROR);
        } else {
            nic_descriptor *rx=&n->rx[n->device_rx_cursor];
            if (atomic_load_explicit(&rx->owner,memory_order_acquire) != NIC_DEVICE) {
                atomic_fetch_add(&n->rx_no_buffer,1);
            } else {
                memcpy(rx->buffer,tx->buffer,tx->length);
                rx->length=tx->length; rx->error=0;
                atomic_store_explicit(&rx->owner,NIC_DONE,memory_order_release);
                n->device_rx_cursor=(n->device_rx_cursor+1)%n->capacity;
                atomic_store(&n->rx_tail,n->device_rx_cursor);
                atomic_fetch_add(&n->rx_completed,1);
                atomic_fetch_or(&n->irq_status,NIC_IRQ_RX);
            }
        }
        atomic_fetch_add(&n->tx_completed,1);
        atomic_store_explicit(&tx->owner,NIC_DONE,memory_order_release);
        n->device_tx_cursor=(n->device_tx_cursor+1)%n->capacity;
        atomic_store(&n->tx_head,n->device_tx_cursor);
        atomic_fetch_or(&n->irq_status,NIC_IRQ_TX);
        ++done;
    }
    return done;
}
unsigned nic_tx_reclaim(nic_model *n, unsigned *errors) {
    if (errors) *errors=0;
    if (!n) return 0;
    unsigned done=0;
    while (done<n->capacity) {
        nic_descriptor *d=&n->tx[n->tx_reclaim_cursor];
        if (atomic_load_explicit(&d->owner,memory_order_acquire) != NIC_DONE) break;
        if (errors && d->error) ++*errors;
        atomic_store_explicit(&d->owner,NIC_FREE,memory_order_release);
        n->tx_reclaim_cursor=(n->tx_reclaim_cursor+1)%n->capacity;
        ++done;
    }
    return done;
}
int nic_rx_receive(nic_model *n, void *packet, size_t capacity, size_t *length) {
    if (!n || !packet || !length) return -1;
    nic_descriptor *d=&n->rx[n->rx_consume_cursor];
    if (atomic_load_explicit(&d->owner,memory_order_acquire) != NIC_DONE) return 1;
    if (capacity<d->length) return -1; // Caller can retry; do not lose the frame.
    memcpy(packet,d->buffer,d->length); *length=d->length;
    atomic_store_explicit(&d->owner,NIC_DEVICE,memory_order_release);
    n->rx_consume_cursor=(n->rx_consume_cursor+1)%n->capacity;
    atomic_store(&n->rx_head,n->rx_consume_cursor);
    return 0;
}
unsigned nic_take_interrupts(nic_model *n) {
    return n ? atomic_exchange_explicit(&n->irq_status,0,memory_order_acq_rel) : 0;
}
void nic_inject_tx_error(nic_model *n) {
    if (n) atomic_store(&n->inject_fault,1);
}
