#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static mlrt_log_level g_level = MLRT_LOG_INFO;

void mlrt_log_set_level(mlrt_log_level level) {
    g_level = level;
}

static const char *level_name(mlrt_log_level level) {
    switch (level) {
        case MLRT_LOG_ERROR: return "ERROR";
        case MLRT_LOG_WARN: return "WARN";
        case MLRT_LOG_INFO: return "INFO";
        case MLRT_LOG_DEBUG: return "DEBUG";
        case MLRT_LOG_TRACE: return "TRACE";
        default: return "?";
    }
}

void mlrt_log(mlrt_log_level level, const char *module, const char *fmt, ...) {
    if (level > g_level) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);

    pthread_mutex_lock(&g_log_lock);
    fprintf(stderr, "[%02d:%02d:%02d.%03ld][%s][%s][tid=%lu] ",
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L,
            module ? module : "CORE", level_name(level),
            (unsigned long)pthread_self());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&g_log_lock);
}
