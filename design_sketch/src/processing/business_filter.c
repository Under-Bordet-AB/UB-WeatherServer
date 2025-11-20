#include "../../include/processing/business_filter.h"
#include "../../include/network/connection.h"
#include <stdlib.h>
#include <string.h>

typedef struct business_filter {
    filter base;
    backend* backend;
} business_filter;

static void business_cb(ub_context* ctx, void* result, void* user_data) {
    (void)user_data;
    // Called inside scheduler task context (backend invoked it). Resume connection.
    if (!ctx)
        return;
    connection* conn = (connection*)ctx->user_data;
    if (!conn)
        return;

    // Transfer ownership of result to connection
    conn->response_data = result;
    conn->response_len = result ? strlen((char*)result) : 0;

    // Trigger FSM event to indicate data ready
    ub_fsm_handle_event(&conn->fsm, &conn->context, (void*)1);
}

static filter_status business_process(filter* self, ub_context* ctx, void* data, void** out_data) {
    business_filter* bf = (business_filter*)self;
    // For demo purposes, build a simple query from request buffer
    (void)data;
    const char* query = "city=stockholm";

    if (!bf->backend)
        return FILTER_STOP;

    int rc = bf->backend->fetch(bf->backend, ctx, query, business_cb, NULL);
    if (rc != 0) {
        return FILTER_STOP;
    }
    // Indicate async processing
    return FILTER_WAIT;
}

static void business_destroy(filter* f) {
    free(f);
}

filter* create_business_filter(backend* backend) {
    business_filter* bf = calloc(1, sizeof(business_filter));
    if (!bf)
        return NULL;
    bf->base.name = "BusinessFilter";
    bf->base.process = business_process;
    bf->base.destroy = business_destroy;
    bf->backend = backend;
    return &bf->base;
}
