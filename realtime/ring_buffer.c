#include "ring_buffer.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static unsigned char *slot(mlrt_ring_buffer *rb, size_t index) {
    return rb->storage + index * rb->item_size;
}

int mlrt_ring_init(mlrt_ring_buffer *rb, void *storage, size_t capacity, size_t item_size) {
    if (!rb || !storage || capacity == 0 || item_size == 0) return -1;
    memset(rb, 0, sizeof(*rb));
    rb->storage = (unsigned char *)storage;
    rb->capacity = capacity;
    rb->item_size = item_size;
    if (pthread_mutex_init(&rb->lock, NULL) != 0) return -1;
    if (pthread_cond_init(&rb->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&rb->lock);
        return -1;
    }
    return 0;
}

void mlrt_ring_destroy(mlrt_ring_buffer *rb) {
    if (!rb) return;
    pthread_cond_destroy(&rb->not_empty);
    pthread_mutex_destroy(&rb->lock);
}

int mlrt_ring_push(mlrt_ring_buffer *rb, const void *item, int overwrite_oldest) {
    if (!rb || !item) return -1;
    pthread_mutex_lock(&rb->lock);
    if (rb->count == rb->capacity) {
        if (!overwrite_oldest) {
            rb->dropped++;
            pthread_mutex_unlock(&rb->lock);
            return 1;
        }
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        rb->dropped++;
    }

    memcpy(slot(rb, rb->head), item, rb->item_size);
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;
    if (rb->count > rb->high_water) rb->high_water = rb->count;
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

int mlrt_ring_pop(mlrt_ring_buffer *rb, void *item) {
    if (!rb || !item) return -1;
    pthread_mutex_lock(&rb->lock);
    if (rb->count == 0) {
        pthread_mutex_unlock(&rb->lock);
        return 1;
    }
    memcpy(item, slot(rb, rb->tail), rb->item_size);
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

static void deadline_from_now(struct timespec *ts, int timeout_ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

int mlrt_ring_wait_pop(mlrt_ring_buffer *rb, void *item, int timeout_ms) {
    if (!rb || !item) return -1;
    pthread_mutex_lock(&rb->lock);
    while (rb->count == 0) {
        if (timeout_ms < 0) {
            if (pthread_cond_wait(&rb->not_empty, &rb->lock) != 0) {
                pthread_mutex_unlock(&rb->lock);
                return -1;
            }
        } else {
            struct timespec deadline;
            deadline_from_now(&deadline, timeout_ms);
            int rc = pthread_cond_timedwait(&rb->not_empty, &rb->lock, &deadline);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&rb->lock);
                return 1;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&rb->lock);
                return -1;
            }
        }
    }

    memcpy(item, slot(rb, rb->tail), rb->item_size);
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

void mlrt_ring_get_stats(mlrt_ring_buffer *rb, mlrt_ring_stats *stats) {
    if (!rb || !stats) return;
    pthread_mutex_lock(&rb->lock);
    stats->count = rb->count;
    stats->capacity = rb->capacity;
    stats->high_water = rb->high_water;
    stats->dropped = rb->dropped;
    pthread_mutex_unlock(&rb->lock);
}
