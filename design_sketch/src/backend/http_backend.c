#include "../../include/backend/http_backend.h"
#include "../../../src/libs/majjen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Implementation details: backend->impl_data points to this struct
typedef struct http_backend_impl {
    mj_scheduler* scheduler;
    char* base_url;
} http_backend_impl;

// Task context used for scheduled async fetch
typedef struct http_fetch_task {
    mj_task task;
    http_backend_impl* impl;
    backend_cb cb;
    void* cb_arg;
    ub_context* req_ctx;
    char* query;
} http_fetch_task;

static void http_fetch_task_run(mj_scheduler* scheduler, void* ctx) {
    http_fetch_task* t = (http_fetch_task*)ctx;

    // Simulate fetching data (in real backend you'd do non-blocking IO here)
    // For the sketch we build a small JSON-like string
    const char* template_fmt = "{\"source\":\"http_backend\",\"query\":\"%s\",\"data\":\"sunny\"}";
    size_t needed = snprintf(NULL, 0, template_fmt, t->query) + 1;
    char* result = malloc(needed);
    if (result) {
        snprintf(result, needed, template_fmt, t->query);
    }

    // Call the callback with the simulated result
    if (t->cb) {
        t->cb(t->req_ctx, result, t->cb_arg);
    }

    // Note: ownership of `result` is transferred to the callback. The
    // callback implementation is responsible for freeing it when done.

    // Free task resources
    free(t->query);
    // Remove self from scheduler and free the mj_task wrapper
    mj_scheduler_task_remove_current(scheduler);
    free(t);
}

static int http_backend_init(backend* self, void* config) {
    // No-op in this simple backend
    (void)self;
    (void)config;
    return 0;
}

static int http_backend_fetch(backend* self, ub_context* ctx, const char* query, backend_cb cb, void* cb_arg) {
    if (!self || !query)
        return -1;
    http_backend_impl* impl = (http_backend_impl*)self->impl_data;
    if (!impl || !impl->scheduler)
        return -1;

    // Allocate task context
    http_fetch_task* t = calloc(1, sizeof(http_fetch_task));
    if (!t)
        return -1;

    t->impl = impl;
    t->cb = cb;
    t->cb_arg = cb_arg;
    t->req_ctx = ctx;
    t->query = strdup(query);

    // Prepare mj_task
    t->task.create = NULL;
    t->task.run = http_fetch_task_run;
    t->task.cleanup = NULL;
    t->task.ctx = t;

    // Schedule the task on the scheduler (async delivery)
    if (mj_scheduler_task_add(impl->scheduler, (mj_task*)&t->task) < 0) {
        free(t->query);
        free(t);
        return -1;
    }

    return 0;
}

static void http_backend_destroy(backend* self) {
    if (!self)
        return;
    http_backend_impl* impl = (http_backend_impl*)self->impl_data;
    if (impl) {
        free(impl->base_url);
        free(impl);
    }
    free(self);
}

backend* ub_http_backend_create(mj_scheduler* scheduler, const char* base_url) {
    if (!scheduler)
        return NULL;

    backend* b = calloc(1, sizeof(backend));
    if (!b)
        return NULL;

    http_backend_impl* impl = calloc(1, sizeof(http_backend_impl));
    if (!impl) {
        free(b);
        return NULL;
    }

    impl->scheduler = scheduler;
    impl->base_url = base_url ? strdup(base_url) : strdup("http://api.example");

    b->name = "http_backend";
    b->impl_data = impl;
    b->init = http_backend_init;
    b->fetch = http_backend_fetch;
    b->destroy = http_backend_destroy;

    return b;
}
