#ifndef MLRT_COMMON_H
#define MLRT_COMMON_H

#include <stddef.h>

void mlrt_die(const char *fmt, ...);
void mlrt_warn(const char *fmt, ...);
void *mlrt_xmalloc(size_t size);
void *mlrt_xcalloc(size_t count, size_t size);
char *mlrt_xstrdup(const char *s);
size_t mlrt_round_up(size_t value, size_t alignment);
int mlrt_parse_u64(const char *text, unsigned long long *value);

#endif
