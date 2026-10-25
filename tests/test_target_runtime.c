#include "target_sim.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static unsigned long long millis(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int main(void) {
    mlrt_target_ctx ctx;
    assert(mlrt_target_init(&ctx, 1) == 0);
    assert(mlrt_target_start(&ctx) == 0);
    usleep(750000);
    assert(atomic_load(&ctx.watchdog_recoveries) == 0);
    assert(atomic_load(&ctx.state) == MLRT_TARGET_RUNNING);
    mlrt_target_stop(&ctx);

    assert(mlrt_target_init(&ctx, 100) == 0);
    assert(mlrt_target_start(&ctx) == 0);
    mlrt_packet req = {.version = 1, .type = MLRT_MSG_FAULT_INJECT,
                       .length = 1, .payload = {1}};
    mlrt_packet rsp;
    assert(mlrt_target_handle_packet(&ctx, &req, &rsp) == 0);
    unsigned long long deadline = millis() + 2500;
    while (atomic_load(&ctx.watchdog_recoveries) == 0 && millis() < deadline)
        usleep(10000);
    assert(atomic_load(&ctx.watchdog_recoveries) == 1);
    assert(atomic_load(&ctx.freeze_until_ns) == 0);
    assert(atomic_load(&ctx.state) == MLRT_TARGET_RUNNING);
    assert(atomic_load(&ctx.processed) > 0);
    mlrt_target_stop(&ctx);
    puts("test_target_runtime: PASS (low-rate liveness + confirmed recovery)");
    return 0;
}
