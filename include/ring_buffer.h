#ifndef MLRT_RING_BUFFER_H
#define MLRT_RING_BUFFER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *storage;
    size_t capacity;
    size_t item_size;
    size_t head;
    size_t tail;
    size_t count;
    size_t high_water;
    uint64_t dropped;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} mlrt_ring_buffer;

typedef struct {
    size_t count;
    size_t capacity;
    size_t high_water;
    uint64_t dropped;
} mlrt_ring_stats;

int mlrt_ring_init(mlrt_ring_buffer *rb, void *storage, size_t capacity, size_t item_size);
void mlrt_ring_destroy(mlrt_ring_buffer *rb);
int mlrt_ring_push(mlrt_ring_buffer *rb, const void *item, int overwrite_oldest);
int mlrt_ring_pop(mlrt_ring_buffer *rb, void *item);
int mlrt_ring_wait_pop(mlrt_ring_buffer *rb, void *item, int timeout_ms);
void mlrt_ring_get_stats(mlrt_ring_buffer *rb, mlrt_ring_stats *stats);

#endif
