#ifndef MLRT_ALLOCATOR_H
#define MLRT_ALLOCATOR_H

#include <stddef.h>

typedef struct {
    size_t mapped_bytes;
    size_t allocated_bytes;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
} MyAllocatorStats;

void *my_malloc(size_t size);
void my_free(void *ptr);
void my_allocator_get_stats(MyAllocatorStats *stats);
void my_allocator_dump_stats(int fd);

#endif
