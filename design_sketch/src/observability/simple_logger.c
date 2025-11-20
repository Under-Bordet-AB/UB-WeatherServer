#include "../../include/observability/logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct simple_logger_impl {
    int level; // not used in sketch, but placeholder
} simple_logger_impl;

static void timestamp(char* buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

static void log_vfmt(FILE* out, const char* level, const char* fmt, va_list ap) {
    char ts[64];
    timestamp(ts, sizeof(ts));
    fprintf(out, "%s [%s] ", ts, level);
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
}

static void simple_info(logger* l, const char* fmt, ...) {
    (void)l;
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stdout, "INFO", fmt, ap);
    va_end(ap);
}

static void simple_warn(logger* l, const char* fmt, ...) {
    (void)l;
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stderr, "WARN", fmt, ap);
    va_end(ap);
}

static void simple_error(logger* l, const char* fmt, ...) {
    (void)l;
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stderr, "ERROR", fmt, ap);
    va_end(ap);
}

static void simple_debug(logger* l, const char* fmt, ...) {
    (void)l;
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stdout, "DEBUG", fmt, ap);
    va_end(ap);
}

static void simple_destroy(logger* l) {
    if (!l)
        return;
    free(l->impl_data);
    free(l);
}

logger* ub_simple_logger_create(void) {
    logger* l = calloc(1, sizeof(logger));
    if (!l)
        return NULL;
    simple_logger_impl* impl = calloc(1, sizeof(simple_logger_impl));
    if (!impl) {
        free(l);
        return NULL;
    }
    impl->level = 0;

    l->info = simple_info;
    l->warn = simple_warn;
    l->error = simple_error;
    l->debug = simple_debug;
    l->destroy = simple_destroy;
    l->impl_data = impl;
    return l;
}
