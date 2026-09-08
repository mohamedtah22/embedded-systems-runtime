#include "common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void vmessage(FILE *stream, const char *prefix, const char *fmt, va_list ap) {
    if (!fmt) fmt = "(no message)";
    if (prefix) {
        fputs(prefix, stream);
    }
    vfprintf(stream, fmt, ap);
    fputc('\n', stream);
}

void mlrt_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vmessage(stderr, "mlrt: error: ", fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void mlrt_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vmessage(stderr, "mlrt: warning: ", fmt, ap);
    va_end(ap);
}

void *mlrt_xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        mlrt_die("out of memory allocating %zu bytes", size);
    }
    return p;
}

void *mlrt_xcalloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (!p) {
        mlrt_die("out of memory allocating %zu bytes", count * size);
    }
    return p;
}

char *mlrt_xstrdup(const char *s) {
    char *copy = strdup(s);
    if (!copy) {
        mlrt_die("out of memory duplicating string");
    }
    return copy;
}

size_t mlrt_round_up(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    size_t rem = value % alignment;
    return rem ? value + (alignment - rem) : value;
}

int mlrt_parse_u64(const char *text, unsigned long long *value) {
    if (!text || !*text || !value) {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }
    *value = parsed;
    return 0;
}
