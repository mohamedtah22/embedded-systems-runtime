#include "target_sim.h"
#include "logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_until(uint64_t deadline_ns) {
    struct timespec ts = {
        .tv_sec = (time_t)(deadline_ns / 1000000000ull),
        .tv_nsec = (long)(deadline_ns % 1000000000ull)
    };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

static uint16_t get_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t clamp_u64_u32(uint64_t v) {
    return v > 0xffffffffull ? 0xffffffffu : (uint32_t)v;
}

const char *mlrt_target_state_name(int state) {
    switch (state) {
        case MLRT_TARGET_BOOT: return "BOOT";
        case MLRT_TARGET_INIT: return "INIT";
        case MLRT_TARGET_READY: return "READY";
        case MLRT_TARGET_RUNNING: return "RUNNING";
        case MLRT_TARGET_FAULT: return "FAULT";
        case MLRT_TARGET_RECOVERY: return "RECOVERY";
        default: return "UNKNOWN";
    }
}

static int32_t synth_value(uint64_t seq) {
    return 25000 + (int32_t)(seq % 200) * 5 - 500;
}

static void *sensor_worker(void *arg) {
    mlrt_target_ctx *ctx = arg;
    uint64_t seq = 0;
    uint64_t next = now_ns();
    while (atomic_load(&ctx->running)) {
        unsigned rate = atomic_load(&ctx->sample_rate_hz);
        if (rate == 0) rate = 1;
        uint64_t period = 1000000000ull / rate;
        next += period;
        sleep_until(next);
        if (!atomic_load(&ctx->running)) break;

        mlrt_target_sample s = {
            .seq = seq,
            .timestamp_ns = now_ns(),
            .milli_value = synth_value(seq)
        };
        if (mlrt_ring_push(&ctx->queue, &s, 0) == 0) atomic_fetch_add(&ctx->produced, 1);
        ++seq;
        if (now_ns() > next + period * 4) next = now_ns();
    }
    return NULL;
}

static void *processing_worker(void *arg) {
    mlrt_target_ctx *ctx = arg;
    while (atomic_load(&ctx->running)) {
        uint64_t freeze_until = atomic_load(&ctx->freeze_until_ns);
        if (freeze_until && now_ns() < freeze_until) {
            usleep(10000);
            continue;
        }
        if (freeze_until && now_ns() >= freeze_until) atomic_store(&ctx->freeze_until_ns, 0);

        mlrt_target_sample s;
        if (mlrt_ring_wait_pop(&ctx->queue, &s, 100) != 0) continue;
        (void)s;
        atomic_fetch_add(&ctx->processed, 1);
        atomic_store(&ctx->last_processed_ns, now_ns());
    }
    return NULL;
}

static void *watchdog_worker(void *arg) {
    mlrt_target_ctx *ctx = arg;
    int latched = 0;
    while (atomic_load(&ctx->running)) {
        usleep(50000);
        uint64_t last = atomic_load(&ctx->last_processed_ns);
        uint64_t age = now_ns() - last;
        if (age > 500000000ull && atomic_load(&ctx->state) == MLRT_TARGET_RUNNING) {
            if (!latched) {
                latched = 1;
                atomic_store(&ctx->state, MLRT_TARGET_FAULT);
                atomic_fetch_add(&ctx->watchdog_recoveries, 1);
                mlrt_log(MLRT_LOG_WARN, "TARGET", "watchdog detected stalled processing task");
                usleep(100000);
                atomic_store(&ctx->state, MLRT_TARGET_RECOVERY);
                atomic_store(&ctx->last_processed_ns, now_ns());
                usleep(50000);
                atomic_store(&ctx->state, MLRT_TARGET_RUNNING);
                mlrt_log(MLRT_LOG_INFO, "TARGET", "watchdog recovery completed");
            }
        } else if (age < 200000000ull) {
            latched = 0;
        }
    }
    return NULL;
}

int mlrt_target_init(mlrt_target_ctx *ctx, unsigned rate_hz) {
    if (!ctx || rate_hz == 0 || rate_hz > 1000) return -1;
    memset(ctx, 0, sizeof(*ctx));
    if (mlrt_ring_init(&ctx->queue, ctx->storage, 128, sizeof(ctx->storage[0])) != 0) return -1;
    atomic_init(&ctx->running, 0);
    atomic_init(&ctx->state, MLRT_TARGET_BOOT);
    atomic_init(&ctx->sample_rate_hz, rate_hz);
    atomic_init(&ctx->started_ns, now_ns());
    atomic_init(&ctx->produced, 0);
    atomic_init(&ctx->processed, 0);
    atomic_init(&ctx->last_processed_ns, now_ns());
    atomic_init(&ctx->watchdog_recoveries, 0);
    atomic_init(&ctx->crc_errors, 0);
    atomic_init(&ctx->freeze_until_ns, 0);
    ctx->threads_started = 0;
    return 0;
}

int mlrt_target_start(mlrt_target_ctx *ctx) {
    if (!ctx) return -1;
    atomic_store(&ctx->state, MLRT_TARGET_INIT);
    atomic_store(&ctx->running, 1);
    atomic_store(&ctx->state, MLRT_TARGET_READY);

    if (pthread_create(&ctx->sensor_thread, NULL, sensor_worker, ctx) != 0) goto fail;
    ctx->threads_started = 1;
    if (pthread_create(&ctx->processing_thread, NULL, processing_worker, ctx) != 0) goto fail;
    ctx->threads_started = 2;
    if (pthread_create(&ctx->watchdog_thread, NULL, watchdog_worker, ctx) != 0) goto fail;
    ctx->threads_started = 3;
    atomic_store(&ctx->state, MLRT_TARGET_RUNNING);
    return 0;

fail:
    atomic_store(&ctx->running, 0);
    if (ctx->threads_started >= 1) pthread_join(ctx->sensor_thread, NULL);
    if (ctx->threads_started >= 2) pthread_join(ctx->processing_thread, NULL);
    ctx->threads_started = 0;
    return -1;
}

void mlrt_target_stop(mlrt_target_ctx *ctx) {
    if (!ctx) return;
    atomic_store(&ctx->running, 0);
    if (ctx->threads_started >= 1) pthread_join(ctx->sensor_thread, NULL);
    if (ctx->threads_started >= 2) pthread_join(ctx->processing_thread, NULL);
    if (ctx->threads_started >= 3) pthread_join(ctx->watchdog_thread, NULL);
    ctx->threads_started = 0;
    mlrt_ring_destroy(&ctx->queue);
}

static void make_error(const mlrt_packet *req, mlrt_packet *rsp, const char *text) {
    memset(rsp, 0, sizeof(*rsp));
    rsp->version = MLRT_PROTO_VERSION;
    rsp->type = MLRT_MSG_ERROR;
    rsp->seq = req ? req->seq : 0;
    size_t n = strlen(text);
    if (n > MLRT_PROTO_MAX_PAYLOAD) n = MLRT_PROTO_MAX_PAYLOAD;
    rsp->length = (uint16_t)n;
    memcpy(rsp->payload, text, n);
}

static void make_status(mlrt_target_ctx *ctx, const mlrt_packet *req, mlrt_packet *rsp) {
    memset(rsp, 0, sizeof(*rsp));
    rsp->version = MLRT_PROTO_VERSION;
    rsp->type = MLRT_MSG_STATUS_RSP;
    rsp->seq = req->seq;
    rsp->length = 27;

    uint64_t uptime_ms = (now_ns() - atomic_load(&ctx->started_ns)) / 1000000ull;
    mlrt_ring_stats qs;
    mlrt_ring_get_stats(&ctx->queue, &qs);
    rsp->payload[0] = (uint8_t)atomic_load(&ctx->state);
    put_u16_be(rsp->payload + 1, (uint16_t)atomic_load(&ctx->sample_rate_hz));
    put_u32_be(rsp->payload + 3, clamp_u64_u32(uptime_ms));
    put_u32_be(rsp->payload + 7, clamp_u64_u32(atomic_load(&ctx->produced)));
    put_u32_be(rsp->payload + 11, clamp_u64_u32(atomic_load(&ctx->processed)));
    put_u32_be(rsp->payload + 15, clamp_u64_u32(qs.dropped));
    put_u32_be(rsp->payload + 19, clamp_u64_u32(atomic_load(&ctx->crc_errors)));
    put_u32_be(rsp->payload + 23, clamp_u64_u32(atomic_load(&ctx->watchdog_recoveries)));
}

static void reset_runtime(mlrt_target_ctx *ctx) {
    atomic_store(&ctx->state, MLRT_TARGET_INIT);
    atomic_store(&ctx->produced, 0);
    atomic_store(&ctx->processed, 0);
    atomic_store(&ctx->watchdog_recoveries, 0);
    atomic_store(&ctx->crc_errors, 0);
    atomic_store(&ctx->freeze_until_ns, 0);
    atomic_store(&ctx->started_ns, now_ns());
    atomic_store(&ctx->last_processed_ns, now_ns());
    usleep(20000);
    atomic_store(&ctx->state, MLRT_TARGET_RUNNING);
}

int mlrt_target_handle_packet(mlrt_target_ctx *ctx, const mlrt_packet *req, mlrt_packet *rsp) {
    if (!ctx || !req || !rsp) return -1;
    switch (req->type) {
        case MLRT_MSG_STATUS_REQ:
            make_status(ctx, req, rsp);
            return 0;
        case MLRT_MSG_CONFIG_SET: {
            if (req->length != 2) { make_error(req, rsp, "CONFIG_SET requires 2-byte rate"); return 0; }
            unsigned rate = get_u16_be(req->payload);
            if (rate == 0 || rate > 1000) { make_error(req, rsp, "rate must be 1..1000 Hz"); return 0; }
            atomic_store(&ctx->sample_rate_hz, rate);
            memset(rsp, 0, sizeof(*rsp));
            rsp->version = MLRT_PROTO_VERSION;
            rsp->type = MLRT_MSG_CONFIG_ACK;
            rsp->seq = req->seq;
            rsp->length = 2;
            put_u16_be(rsp->payload, (uint16_t)rate);
            return 0;
        }
        case MLRT_MSG_FAULT_INJECT:
            if (req->length != 1 || req->payload[0] != 1) { make_error(req, rsp, "supported fault: freeze-worker"); return 0; }
            atomic_store(&ctx->freeze_until_ns, now_ns() + 900000000ull);
            memset(rsp, 0, sizeof(*rsp));
            rsp->version = MLRT_PROTO_VERSION;
            rsp->type = MLRT_MSG_CONFIG_ACK;
            rsp->seq = req->seq;
            rsp->length = 1;
            rsp->payload[0] = 1;
            return 0;
        case MLRT_MSG_RESET:
            reset_runtime(ctx);
            memset(rsp, 0, sizeof(*rsp));
            rsp->version = MLRT_PROTO_VERSION;
            rsp->type = MLRT_MSG_RESET_ACK;
            rsp->seq = req->seq;
            return 0;
        case MLRT_MSG_FW_INFO_REQ: {
            memset(rsp, 0, sizeof(*rsp));
            rsp->version = MLRT_PROTO_VERSION;
            rsp->type = MLRT_MSG_FW_INFO_RSP;
            rsp->seq = req->seq;
#if defined(__aarch64__)
            const char *arch = "arm64";
#elif defined(__x86_64__)
            const char *arch = "x86_64";
#elif defined(__i386__)
            const char *arch = "i386";
#else
            const char *arch = "unknown";
#endif
            int n = snprintf((char *)rsp->payload, sizeof(rsp->payload), "target-sim/1.0 arch=%s", arch);
            if (n < 0) n = 0;
            if ((size_t)n > sizeof(rsp->payload)) n = (int)sizeof(rsp->payload);
            rsp->length = (uint16_t)n;
            return 0;
        }
        default:
            make_error(req, rsp, "unsupported message type");
            return 0;
    }
}
