#include "../../include/backend/cache_backend.h"
#include "../../../src/libs/majjen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple cache entry
typedef struct cache_entry {
    char* key;
    char* value; // owned
    struct cache_entry* next;
} cache_entry;

typedef struct cache_backend_impl {
    mj_scheduler* scheduler;
    cache_entry* head;
    size_t max_entries;
} cache_backend_impl;

// Task context for async delivery
typedef struct cache_fetch_task {
    mj_task task;
    cache_backend_impl* impl;
    backend_cb cb;
    void* cb_arg;
    ub_context* req_ctx;
    char* key;
} cache_fetch_task;

static cache_entry* cache_get(cache_backend_impl* impl, const char* key) {
    cache_entry* cur = impl->head;
    while (cur) {
        if (strcmp(cur->key, key) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static void cache_put(cache_backend_impl* impl, const char* key, const char* value) {
    cache_entry* e = malloc(sizeof(cache_entry));
    e->key = strdup(key);
    e->value = strdup(value);
    e->next = impl->head;
    impl->head = e;
}

static void cache_fetch_task_run(mj_scheduler* scheduler, void* ctx) {
    cache_fetch_task* t = (cache_fetch_task*)ctx;
    cache_backend_impl* impl = t->impl;

    cache_entry* e = cache_get(impl, t->key);
    char* payload = NULL;
    if (e) {
        // Cache hit
        payload = strdup(e->value);
    } else {
        // Cache miss -> simulate fetching and populate cache
        const char* template_fmt = "{\"source\":\"cache_backend\",\"query\":\"%s\",\"data\":\"generated\"}";
        size_t needed = snprintf(NULL, 0, template_fmt, t->key) + 1;
        payload = malloc(needed);
        snprintf(payload, needed, template_fmt, t->key);
        cache_put(impl, t->key, payload);
    }

    if (t->cb)
        t->cb(t->req_ctx, payload, t->cb_arg);
    // Ownership of payload is transferred to the callback; callback must free it.
    free(t->key);
    mj_scheduler_task_remove_current(scheduler);
    free(t);
}

static int cache_backend_init(backend* self, void* config) {
    (void)self;
    (void)config;
    return 0;
}

static int cache_backend_fetch(backend* self, ub_context* ctx, const char* query, backend_cb cb, void* cb_arg) {
    if (!self || !query)
        return -1;
    cache_backend_impl* impl = (cache_backend_impl*)self->impl_data;
    if (!impl || !impl->scheduler)
        return -1;

    cache_fetch_task* t = calloc(1, sizeof(cache_fetch_task));
    if (!t)
        return -1;
    t->impl = impl;
    t->cb = cb;
    t->cb_arg = cb_arg;
    t->req_ctx = ctx;
    t->key = strdup(query);

    t->task.create = NULL;
    t->task.run = cache_fetch_task_run;
    t->task.cleanup = NULL;
    t->task.ctx = t;

    if (mj_scheduler_task_add(impl->scheduler, (mj_task*)&t->task) < 0) {
        free(t->key);
        free(t);
        return -1;
    }
    return 0;
}

static void cache_backend_destroy(backend* self) {
    if (!self)
        return;
    cache_backend_impl* impl = (cache_backend_impl*)self->impl_data;
    if (impl) {
        cache_entry* cur = impl->head;
        while (cur) {
            cache_entry* next = cur->next;
            free(cur->key);
            free(cur->value);
            free(cur);
            cur = next;
        }
        free(impl);
    }
    free(self);
}

backend* ub_cache_backend_create(mj_scheduler* scheduler, size_t max_entries) {
    if (!scheduler)
        return NULL;

    backend* b = calloc(1, sizeof(backend));
    if (!b)
        return NULL;

    cache_backend_impl* impl = calloc(1, sizeof(cache_backend_impl));
    if (!impl) {
        free(b);
        return NULL;
    }

    impl->scheduler = scheduler;
    impl->head = NULL;
    impl->max_entries = max_entries;

    b->name = "cache_backend";
    b->impl_data = impl;
    b->init = cache_backend_init;
    b->fetch = cache_backend_fetch;
    b->destroy = cache_backend_destroy;

    return b;
}
