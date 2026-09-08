#include "ring_buffer.h"

#include <assert.h>
#include <stdio.h>

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
    mlrt_ring_destroy(&rb);
    puts("test_ring_buffer: PASS");
    return 0;
}
