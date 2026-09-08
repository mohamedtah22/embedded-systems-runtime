#ifndef MLRT_LOGGER_H
#define MLRT_LOGGER_H

typedef enum {
    MLRT_LOG_ERROR = 0,
    MLRT_LOG_WARN = 1,
    MLRT_LOG_INFO = 2,
    MLRT_LOG_DEBUG = 3,
    MLRT_LOG_TRACE = 4
} mlrt_log_level;

void mlrt_log_set_level(mlrt_log_level level);
void mlrt_log(mlrt_log_level level, const char *module, const char *fmt, ...);

#endif
