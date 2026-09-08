#include "allocator.h"
#include "common.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGNMENT 16UL
#define ARENA_MIN (64UL * 1024UL)
#define BLOCK_MAGIC 0x4d4c5254424c4f43ULL

typedef struct BlockHeader {
    uint64_t magic;
    size_t size;
    int free;
    struct BlockHeader *prev;
    struct BlockHeader *next;
} BlockHeader;

static BlockHeader *head = NULL;
static pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t mapped_bytes = 0;

static size_t aligned_size(size_t n) {
    return mlrt_round_up(n, ALIGNMENT);
}

static void insert_sorted(BlockHeader *block) {
    if (!head || block < head) {
        block->next = head;
        block->prev = NULL;
        if (head) head->prev = block;
        head = block;
        return;
    }
    BlockHeader *cur = head;
    while (cur->next && cur->next < block) cur = cur->next;
    block->next = cur->next;
    block->prev = cur;
    if (cur->next) cur->next->prev = block;
    cur->next = block;
}

static BlockHeader *map_arena(size_t minimum_payload) {
    long page_long = sysconf(_SC_PAGESIZE);
    size_t page = page_long > 0 ? (size_t)page_long : 4096;
    size_t total = aligned_size(sizeof(BlockHeader)) + minimum_payload;
    if (total < ARENA_MIN) total = ARENA_MIN;
    total = mlrt_round_up(total, page);
    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;
    mapped_bytes += total;
    BlockHeader *block = mem;
    block->magic = BLOCK_MAGIC;
    block->size = total - aligned_size(sizeof(BlockHeader));
    block->free = 1;
    block->prev = block->next = NULL;
    insert_sorted(block);
    return block;
}

static BlockHeader *find_fit(size_t size) {
    for (BlockHeader *b = head; b; b = b->next) {
        if (b->free && b->size >= size) return b;
    }
    return NULL;
}

static void split_block(BlockHeader *block, size_t size) {
    size_t header_size = aligned_size(sizeof(BlockHeader));
    if (block->size < size + header_size + ALIGNMENT) return;
    unsigned char *payload = (unsigned char *)block + header_size;
    BlockHeader *tail = (BlockHeader *)(payload + size);
    tail->magic = BLOCK_MAGIC;
    tail->size = block->size - size - header_size;
    tail->free = 1;
    tail->prev = block;
    tail->next = block->next;
    if (tail->next) tail->next->prev = tail;
    block->next = tail;
    block->size = size;
}

static int adjacent(BlockHeader *a, BlockHeader *b) {
    size_t header_size = aligned_size(sizeof(BlockHeader));
    return (unsigned char *)a + header_size + a->size == (unsigned char *)b;
}

static void coalesce_with_next(BlockHeader *block) {
    BlockHeader *next = block->next;
    if (!next || !next->free || !adjacent(block, next)) return;
    size_t header_size = aligned_size(sizeof(BlockHeader));
    block->size += header_size + next->size;
    block->next = next->next;
    if (block->next) block->next->prev = block;
}

void *my_malloc(size_t size) {
    if (size == 0) return NULL;
    size = aligned_size(size);
    pthread_mutex_lock(&allocator_lock);
    BlockHeader *block = find_fit(size);
    if (!block) block = map_arena(size);
    if (!block) {
        pthread_mutex_unlock(&allocator_lock);
        return NULL;
    }
    split_block(block, size);
    block->free = 0;
    void *payload = (unsigned char *)block + aligned_size(sizeof(BlockHeader));
    pthread_mutex_unlock(&allocator_lock);
    return payload;
}

void my_free(void *ptr) {
    if (!ptr) return;
    size_t header_size = aligned_size(sizeof(BlockHeader));
    BlockHeader *block = (BlockHeader *)((unsigned char *)ptr - header_size);
    pthread_mutex_lock(&allocator_lock);
    if (block->magic != BLOCK_MAGIC) {
        pthread_mutex_unlock(&allocator_lock);
        return;
    }
    block->free = 1;
    coalesce_with_next(block);
    if (block->prev && block->prev->free && adjacent(block->prev, block)) {
        block = block->prev;
        coalesce_with_next(block);
    }
    pthread_mutex_unlock(&allocator_lock);
}

void my_allocator_get_stats(MyAllocatorStats *stats) {
    memset(stats, 0, sizeof(*stats));
    pthread_mutex_lock(&allocator_lock);
    stats->mapped_bytes = mapped_bytes;
    for (BlockHeader *b = head; b; b = b->next) {
        if (b->free) {
            stats->free_blocks++;
            stats->free_bytes += b->size;
        } else {
            stats->allocated_blocks++;
            stats->allocated_bytes += b->size;
        }
    }
    pthread_mutex_unlock(&allocator_lock);
}

void my_allocator_dump_stats(int fd) {
    MyAllocatorStats s;
    my_allocator_get_stats(&s);
    double fragmentation = s.free_bytes ? (double)s.free_blocks / (double)s.free_bytes : 0.0;
    dprintf(fd,
            "allocator stats:\n"
            "  mapped bytes:    %zu\n"
            "  allocated bytes: %zu in %zu blocks\n"
            "  free bytes:      %zu in %zu blocks\n"
            "  free-block density: %.6f blocks/byte\n",
            s.mapped_bytes, s.allocated_bytes, s.allocated_blocks,
            s.free_bytes, s.free_blocks, fragmentation);
}
