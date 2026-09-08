#include "mlrt.h"
#include "ring_buffer.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct { uint64_t value; } bench_item;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int timer_bench(int iterations, int period_us) {
    uint64_t period_ns=(uint64_t)period_us*1000ull;
    uint64_t next=now_ns()+period_ns;
    uint64_t sum=0,max=0;
    for(int i=0;i<iterations;i++){
        struct timespec ts={.tv_sec=(time_t)(next/1000000000ull),.tv_nsec=(long)(next%1000000000ull)};
        while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&ts,NULL)==EINTR){}
        uint64_t actual=now_ns();
        uint64_t jitter=actual>next?actual-next:next-actual;
        sum+=jitter;if(jitter>max)max=jitter;
        next+=period_ns;
    }
    printf("Timer jitter (%dus period)\n  mean: %.2f us\n  max:  %.2f us\n",
           period_us,(double)sum/(double)iterations/1000.0,(double)max/1000.0);
    return 0;
}

static int socketpair_bench(int iterations) {
    int sv[2];
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sv)!=0){perror("socketpair");return 1;}
    uint8_t msg[64]={0},out[64];
    uint64_t start=now_ns();
    for(int i=0;i<iterations;i++){
        if(write(sv[0],msg,sizeof(msg))!=(ssize_t)sizeof(msg)){close(sv[0]);close(sv[1]);return 1;}
        size_t off=0;
        while(off<sizeof(out)){
            ssize_t n=read(sv[1],out+off,sizeof(out)-off);
            if(n<=0){close(sv[0]);close(sv[1]);return 1;}
            off+=(size_t)n;
        }
    }
    uint64_t elapsed=now_ns()-start;
    close(sv[0]);close(sv[1]);
    printf("Unix socket IPC (64-byte one-way messages)\n  messages: %d\n  avg: %.2f us/message\n  throughput: %.2f MB/s\n",
           iterations,(double)elapsed/(double)iterations/1000.0,
           ((double)iterations*64.0)/(double)elapsed*1000.0);
    return 0;
}

static int ring_bench(int iterations) {
    bench_item storage[256];
    mlrt_ring_buffer rb;
    if(mlrt_ring_init(&rb,storage,256,sizeof(storage[0]))!=0)return 1;
    bench_item in={0},out;
    uint64_t start=now_ns();
    for(int i=0;i<iterations;i++){
        in.value=(uint64_t)i;
        if(mlrt_ring_push(&rb,&in,0)!=0 || mlrt_ring_pop(&rb,&out)!=0){mlrt_ring_destroy(&rb);return 1;}
    }
    uint64_t elapsed=now_ns()-start;
    mlrt_ring_destroy(&rb);
    printf("Bounded ring buffer push+pop\n  operations: %d pairs\n  avg: %.2f ns/pair\n",
           iterations,(double)elapsed/(double)iterations);
    return 0;
}

static void usage(FILE *out){fprintf(out,"usage: mlrt benchmark [--iterations N] [--timer-period-us N]\n");}

int benchmark_cli_main(int argc,char **argv){
    int iterations=1000;
    int period_us=1000;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--iterations")==0 && i+1<argc)iterations=atoi(argv[++i]);
        else if(strcmp(argv[i],"--timer-period-us")==0 && i+1<argc)period_us=atoi(argv[++i]);
        else if(strcmp(argv[i],"--help")==0){usage(stdout);return 0;}
        else {usage(stderr);return 2;}
    }
    if(iterations<10||iterations>1000000||period_us<100||period_us>100000){fprintf(stderr,"mlrt benchmark: invalid argument\n");return 2;}
    printf("MLRT embedded-systems benchmark\n");
    if(timer_bench(iterations<5000?iterations:5000,period_us)!=0)return 1;
    if(socketpair_bench(iterations)!=0)return 1;
    if(ring_bench(iterations*10)!=0)return 1;
    return 0;
}
