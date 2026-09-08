#include "mlrt.h"
#include "ring_buffer.h"
#include "logger.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t seq;
    uint64_t timestamp_ns;
    double value;
} sample_t;

typedef struct {
    mlrt_ring_buffer queue;
    sample_t storage[128];
    atomic_bool running;
    atomic_uint_fast64_t produced;
    atomic_uint_fast64_t processed;
    atomic_uint_fast64_t deadline_misses;
    atomic_uint_fast64_t watchdog_events;
    atomic_uint_fast64_t last_processed_ns;
    atomic_int stall_once;
    int period_ms;
    int inject_stall_ms;
    uint64_t jitter_sum_ns;
    uint64_t jitter_max_ns;
    pthread_mutex_t metric_lock;
} rt_ctx;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static struct timespec ns_to_ts(uint64_t ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    return ts;
}

static double synthetic_sensor(uint64_t seq) {
    return 25.0 + (double)(seq % 100) * 0.01 + sin((double)(seq % 32) * 0.1963495408) * 0.25;
}

static void *sensor_thread(void *arg) {
    rt_ctx *ctx = arg;
    const uint64_t period_ns = (uint64_t)ctx->period_ms * 1000000ull;
    uint64_t next = now_ns() + period_ns;
    uint64_t seq = 0;

    while (atomic_load(&ctx->running)) {
        struct timespec wake = ns_to_ts(next);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, NULL) == EINTR) {}
        uint64_t actual = now_ns();
        uint64_t jitter = actual > next ? actual - next : next - actual;
        if (actual > next + period_ns) atomic_fetch_add(&ctx->deadline_misses, 1);

        pthread_mutex_lock(&ctx->metric_lock);
        ctx->jitter_sum_ns += jitter;
        if (jitter > ctx->jitter_max_ns) ctx->jitter_max_ns = jitter;
        pthread_mutex_unlock(&ctx->metric_lock);

        sample_t s = {.seq = seq, .timestamp_ns = actual, .value = synthetic_sensor(seq)};
        if (mlrt_ring_push(&ctx->queue, &s, 0) == 0) atomic_fetch_add(&ctx->produced, 1);
        ++seq;
        next += period_ns;

        if (actual > next + period_ns * 4) next = actual + period_ns;
    }
    return NULL;
}

static void *processing_thread(void *arg) {
    rt_ctx *ctx = arg;
    double filtered = 0.0;
    int have_value = 0;

    while (atomic_load(&ctx->running)) {
        sample_t s;
        int rc = mlrt_ring_wait_pop(&ctx->queue, &s, 100);
        if (rc != 0) continue;

        if (ctx->inject_stall_ms > 0 && atomic_exchange(&ctx->stall_once, 0)) {
            mlrt_log(MLRT_LOG_WARN, "RT", "injecting processing stall for %d ms", ctx->inject_stall_ms);
            usleep((useconds_t)ctx->inject_stall_ms * 1000u);
        }

        filtered = have_value ? filtered * 0.8 + s.value * 0.2 : s.value;
        have_value = 1;
        (void)filtered;
        atomic_fetch_add(&ctx->processed, 1);
        atomic_store(&ctx->last_processed_ns, now_ns());
    }
    return NULL;
}

static void *watchdog_thread(void *arg) {
    rt_ctx *ctx = arg;
    uint64_t threshold_ns = 250000000ull;
    int latched = 0;
    while (atomic_load(&ctx->running)) {
        usleep(50000);
        uint64_t last = atomic_load(&ctx->last_processed_ns);
        uint64_t age = now_ns() - last;
        if (last != 0 && age > threshold_ns) {
            if (!latched) {
                atomic_fetch_add(&ctx->watchdog_events, 1);
                mlrt_log(MLRT_LOG_WARN, "WATCHDOG", "processing heartbeat late by %.1f ms", (double)age / 1e6);
                latched = 1;
            }
        } else {
            latched = 0;
        }
    }
    return NULL;
}

static void rt_usage(FILE *out) {
    fprintf(out,
            "usage: mlrt rt-demo [--duration SEC] [--period-ms N] [--inject-stall-ms N]\n"
            "Runs an embedded-style periodic producer -> bounded ring buffer -> consumer pipeline.\n");
}

int rt_demo_cli_main(int argc, char **argv) {
    int duration = 2;
    int period_ms = 10;
    int inject_stall_ms = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) duration = atoi(argv[++i]);
        else if (strcmp(argv[i], "--period-ms") == 0 && i + 1 < argc) period_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--inject-stall-ms") == 0 && i + 1 < argc) inject_stall_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) { rt_usage(stdout); return 0; }
        else { rt_usage(stderr); return 2; }
    }
    if (duration <= 0 || duration > 60 || period_ms <= 0 || period_ms > 1000 || inject_stall_ms < 0 || inject_stall_ms > 10000) {
        fprintf(stderr, "mlrt rt-demo: invalid timing argument\n");
        return 2;
    }

    rt_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.period_ms = period_ms;
    ctx.inject_stall_ms = inject_stall_ms;
    atomic_init(&ctx.running, 1);
    atomic_init(&ctx.produced, 0);
    atomic_init(&ctx.processed, 0);
    atomic_init(&ctx.deadline_misses, 0);
    atomic_init(&ctx.watchdog_events, 0);
    atomic_init(&ctx.last_processed_ns, now_ns());
    atomic_init(&ctx.stall_once, inject_stall_ms > 0 ? 1 : 0);
    pthread_mutex_init(&ctx.metric_lock, NULL);
    if (mlrt_ring_init(&ctx.queue, ctx.storage, 128, sizeof(sample_t)) != 0) {
        fprintf(stderr, "mlrt rt-demo: ring buffer init failed\n");
        pthread_mutex_destroy(&ctx.metric_lock);
        return 1;
    }

    pthread_t sensor, processor, watchdog;
    if (pthread_create(&sensor, NULL, sensor_thread, &ctx) != 0 ||
        pthread_create(&processor, NULL, processing_thread, &ctx) != 0 ||
        pthread_create(&watchdog, NULL, watchdog_thread, &ctx) != 0) {
        fprintf(stderr, "mlrt rt-demo: thread creation failed\n");
        atomic_store(&ctx.running, 0);
        mlrt_ring_destroy(&ctx.queue);
        pthread_mutex_destroy(&ctx.metric_lock);
        return 1;
    }

    sleep((unsigned)duration);
    atomic_store(&ctx.running, 0);
    pthread_join(sensor, NULL);
    pthread_join(processor, NULL);
    pthread_join(watchdog, NULL);

    mlrt_ring_stats stats;
    mlrt_ring_get_stats(&ctx.queue, &stats);
    uint64_t produced = atomic_load(&ctx.produced);
    uint64_t processed = atomic_load(&ctx.processed);
    uint64_t misses = atomic_load(&ctx.deadline_misses);
    uint64_t wd = atomic_load(&ctx.watchdog_events);

    pthread_mutex_lock(&ctx.metric_lock);
    double avg_jitter_us = produced ? (double)ctx.jitter_sum_ns / (double)produced / 1000.0 : 0.0;
    double max_jitter_us = (double)ctx.jitter_max_ns / 1000.0;
    pthread_mutex_unlock(&ctx.metric_lock);

    printf("Embedded Runtime Demo\n");
    printf("  period:             %d ms\n", period_ms);
    printf("  produced samples:   %llu\n", (unsigned long long)produced);
    printf("  processed samples:  %llu\n", (unsigned long long)processed);
    printf("  queue high-water:   %zu / %zu\n", stats.high_water, stats.capacity);
    printf("  dropped samples:    %llu\n", (unsigned long long)stats.dropped);
    printf("  average jitter:     %.2f us\n", avg_jitter_us);
    printf("  maximum jitter:     %.2f us\n", max_jitter_us);
    printf("  deadline misses:    %llu\n", (unsigned long long)misses);
    printf("  watchdog events:    %llu\n", (unsigned long long)wd);

    mlrt_ring_destroy(&ctx.queue);
    pthread_mutex_destroy(&ctx.metric_lock);
    return 0;
}
