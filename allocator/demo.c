#include "allocator.h"
#include "mlrt.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void *stress_worker(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg ^ (unsigned)time(NULL);
    for (int i = 0; i < 2000; ++i) {
        size_t n = 1 + (rand_r(&seed) % 512);
        unsigned char *p = my_malloc(n);
        if (!p) return (void *)1;
        memset(p, (int)(n & 0xff), n);
        my_free(p);
    }
    return NULL;
}

int allocator_cli_main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Running allocator split/coalesce and threaded stress demo...\n");
    void *a = my_malloc(128);
    void *b = my_malloc(256);
    void *c = my_malloc(512);
    printf("allocated: a=%p b=%p c=%p\n", a, b, c);
    my_free(b);
    my_free(a);
    my_allocator_dump_stats(1);
    my_free(c);

    pthread_t threads[4];
    for (uintptr_t i = 0; i < 4; ++i) pthread_create(&threads[i], NULL, stress_worker, (void *)(i + 1));
    int failed = 0;
    for (int i = 0; i < 4; ++i) {
        void *result = NULL;
        pthread_join(threads[i], &result);
        if (result) failed = 1;
    }
    my_allocator_dump_stats(1);
    return failed;
}
