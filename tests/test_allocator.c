#include "allocator.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void *worker(void *arg) {
    uintptr_t id = (uintptr_t)arg;
    for (int i = 0; i < 1000; ++i) {
        size_t n = 32 + ((i * 17 + id) % 1024);
        unsigned char *p = my_malloc(n);
        if (!p) return (void *)1;
        memset(p, (int)id, n);
        my_free(p);
    }
    return NULL;
}

int main(void) {
    void *a = my_malloc(100);
    void *b = my_malloc(200);
    if (!a || !b) return 1;
    memset(a, 0xaa, 100);
    memset(b, 0xbb, 200);
    my_free(a);
    my_free(b);

    pthread_t t[4];
    for (uintptr_t i = 0; i < 4; ++i) pthread_create(&t[i], NULL, worker, (void *)(i + 1));
    for (int i = 0; i < 4; ++i) {
        void *result = NULL;
        pthread_join(t[i], &result);
        if (result) return 1;
    }
    MyAllocatorStats stats;
    my_allocator_get_stats(&stats);
    if (!stats.mapped_bytes || stats.allocated_blocks != 0 || stats.free_blocks == 0) return 1;
    printf("test_allocator: PASS\n");
    return 0;
}
