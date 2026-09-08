#ifndef MLRT_TARGET_SIM_H
#define MLRT_TARGET_SIM_H

#include "embedded_protocol.h"
#include "ring_buffer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef enum {
    MLRT_TARGET_BOOT = 0,
    MLRT_TARGET_INIT = 1,
    MLRT_TARGET_READY = 2,
    MLRT_TARGET_RUNNING = 3,
    MLRT_TARGET_FAULT = 4,
    MLRT_TARGET_RECOVERY = 5
} mlrt_target_state;

typedef struct {
    uint64_t seq;
    uint64_t timestamp_ns;
    int32_t milli_value;
} mlrt_target_sample;

typedef struct {
    mlrt_ring_buffer queue;
    mlrt_target_sample storage[128];
    atomic_bool running;
    atomic_int state;
    atomic_uint sample_rate_hz;
    atomic_uint_fast64_t started_ns;
    atomic_uint_fast64_t produced;
    atomic_uint_fast64_t processed;
    atomic_uint_fast64_t last_processed_ns;
    atomic_uint_fast64_t watchdog_recoveries;
    atomic_uint_fast64_t crc_errors;
    atomic_uint_fast64_t freeze_until_ns;
    pthread_t sensor_thread;
    pthread_t processing_thread;
    pthread_t watchdog_thread;
    int threads_started;
} mlrt_target_ctx;

int mlrt_target_init(mlrt_target_ctx *ctx, unsigned rate_hz);
int mlrt_target_start(mlrt_target_ctx *ctx);
void mlrt_target_stop(mlrt_target_ctx *ctx);
int mlrt_target_handle_packet(mlrt_target_ctx *ctx, const mlrt_packet *req, mlrt_packet *rsp);
const char *mlrt_target_state_name(int state);
int target_server_cli_main(int argc, char **argv);
int target_client_cli_main(int argc, char **argv);
int target_server_self_test(void);

#endif
