#include "ring_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdint.h>

static atomic_bool waking;
static void *spurious_waker(void *arg) {
    mlrt_ring_buffer *rb = arg;
    for (unsigned i=0; i<200 && atomic_load(&waking); ++i) {
        usleep(5000);
        pthread_mutex_lock(&rb->lock);
        pthread_cond_signal(&rb->not_empty);
        pthread_mutex_unlock(&rb->lock);
    }
    return NULL;
}
static unsigned long long ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000 + t.tv_nsec;
}
int main(void) {
    int storage[3];
    mlrt_ring_buffer rb;
    assert(mlrt_ring_init(&rb, storage, 3, sizeof(int)) == 0);
    int a=1,b=2,c=3,d=4,out=0;
    assert(mlrt_ring_push(&rb,&a,0)==0);
    assert(mlrt_ring_push(&rb,&b,0)==0);
    assert(mlrt_ring_push(&rb,&c,0)==0);
    assert(mlrt_ring_push(&rb,&d,0)==1);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==1);
    assert(mlrt_ring_push(&rb,&d,0)==0);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==2);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==3);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==4);
    assert(mlrt_ring_pop(&rb,&out)==1);

    assert(mlrt_ring_push(&rb,&a,0)==0);
    assert(mlrt_ring_push(&rb,&b,0)==0);
    assert(mlrt_ring_push(&rb,&c,0)==0);
    assert(mlrt_ring_push(&rb,&d,1)==0);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==2);
    mlrt_ring_stats s;
    mlrt_ring_get_stats(&rb,&s);
    assert(s.dropped==2);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==3);
    assert(mlrt_ring_pop(&rb,&out)==0 && out==4);
    pthread_t waker;
    atomic_init(&waking, 1);
    assert(pthread_create(&waker, NULL, spurious_waker, &rb) == 0);
    unsigned long long begin = ns();
    assert(mlrt_ring_wait_pop(&rb, &out, 40) == 1);
    unsigned long long elapsed = ns() - begin;
    atomic_store(&waking, 0);
    pthread_join(waker, NULL);
    assert(elapsed < 500000000ull);
    mlrt_ring_destroy(&rb);
    assert(mlrt_ring_init(&rb, storage, SIZE_MAX, 2) == -1);
    puts("test_ring_buffer: PASS");
    return 0;
}

