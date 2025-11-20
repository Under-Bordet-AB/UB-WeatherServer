#include "../include/backend/backend_interface.h"
#include "../include/core/server.h"
#include "../include/network/listener.h"
#include "../include/processing/business_filter.h"
#include "../include/processing/fsm.h"
#include "../include/processing/pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Use the project's scheduler implementation as the top-level scheduler
#include "../../src/libs/majjen.h"
// observability
#include "../include/observability/logger.h"
#include "../include/observability/metrics.h"
// backends
#include "../include/backend/cache_backend.h"
#include "../include/backend/http_backend.h"

// ==========================================
// 1. Example Filter Implementation: Auth
// ==========================================

filter_status auth_filter_process(filter* self, ub_context* ctx, void* data, void** out_data) {
    // Cast data to http_request* (hypothetical)
    // char* auth_header = get_header(data, "Authorization");
    char* auth_header = "Bearer token"; // Mock

    if (auth_header && strcmp(auth_header, "Bearer token") == 0) {
        // Auth success
        *out_data = data; // Pass through
        return FILTER_CONTINUE;
    } else {
        // Auth failed
        // set_response(ctx, 401, "Unauthorized");
        return FILTER_STOP;
    }
}

filter* create_auth_filter() {
    filter* f = malloc(sizeof(filter));
    f->name = "AuthFilter";
    f->process = auth_filter_process;
    f->destroy = free;
    return f;
}

// ==========================================
// 2. Example FSM State Handlers
// ==========================================

// States
enum {
    STATE_IDLE,
    STATE_READING,
    STATE_PROCESSING,
    STATE_WRITING
};

state_id on_idle_enter(fsm* fsm, ub_context* ctx, void* event_data) {
    printf("Connection %lu: Ready to receive\n", ctx->request_id);
    return STATE_READING; // Auto-transition to reading
}

state_id on_reading_event(fsm* fsm, ub_context* ctx, void* event_data) {
    // Check if we have a full request
    int bytes_read = *(int*)event_data;
    if (bytes_read > 0) {
        printf("Read %d bytes\n", bytes_read);
        return STATE_PROCESSING;
    }
    return STATE_READING; // Keep reading
}

state_id on_processing_enter(fsm* fsm, ub_context* ctx, void* event_data) {
    // Execute Pipeline
    pipeline* pipeline = (pipeline*)fsm->user_data;
    void* request = NULL; // Get from buffer
    void* response = NULL;

    filter_status status = ub_pipeline_execute(pipeline, ctx, request, &response);

    if (status == FILTER_WAIT) {
        // Async backend fetch started
        return STATE_PROCESSING; // Stay here until callback
    }

    return STATE_WRITING;
}

// ==========================================
// 3. Wiring it all up (Main)
// ==========================================

int main() {
    // 1. Setup Configuration
    ub_server_config config = {.bind_address = "0.0.0.0", .port = "8080", .max_connections = 1000};

    // 2. Create top-level scheduler and server that uses it
    mj_scheduler* top_sched = mj_scheduler_create();
    if (!top_sched) {
        fprintf(stderr, "Failed to create top-level scheduler\n");
        return 1;
    }

    // Server will not run or destroy the scheduler; the scheduler is the
    // program's master and is owned by the caller.
    ub_server* server = ub_server_create_with_scheduler(&config, top_sched);

    // Create simple logger and metrics recorder and attach to server
    logger* logger = ub_simple_logger_create();
    metrics_recorder* metrics = ub_simple_metrics_create();
    ub_server_set_logger(server, logger);
    ub_server_set_metrics(server, metrics);

    // Create and register example backends
    backend* http_backend = ub_http_backend_create(top_sched, "http://api.example");
    backend* cache_backend = ub_cache_backend_create(top_sched, 1024);
    ub_server_register_backend(server, "http", http_backend);
    ub_server_register_backend(server, "cache", cache_backend);

    // 3. Setup Pipeline (Global or per-connection)
    // In a real app, we might create a factory for this
    pipeline* main_pipeline = ub_pipeline_create();
    ub_pipeline_add_filter(main_pipeline, create_auth_filter());
    ub_pipeline_add_filter(main_pipeline, create_business_filter(cache_backend));
    // ub_pipeline_add_filter(main_pipeline, create_ratelimit_filter());
    // ub_pipeline_add_filter(main_pipeline, create_business_logic_filter());

    // 4. Setup Listener
    listener* http = ub_create_http_listener();
    // ub_server_add_listener(server, http);

    // 5. Start listeners and run scheduler
    printf("Starting server...\n");
    // Register listeners/backends here
    // e.g. listener* http = ub_create_http_listener();
    listener* http = ub_create_http_listener();
    // Attach the pipeline so new connections inherit it
    ub_http_listener_set_pipeline(http, main_pipeline);
    ub_server_add_listener(server, http);

    // Start listeners (register their tasks on the external scheduler)
    ub_server_start_listeners(server);

    // Run the scheduler (external master loop)
    int rc = mj_scheduler_run(top_sched);

    // Cleanup
    ub_pipeline_destroy(main_pipeline);
    // Stop listeners and cleanup
    ub_server_stop_listeners(server);

    // Destroy backends we created (the server keeps its own registry copy in the sketch)
    if (http_backend && http_backend->destroy)
        http_backend->destroy(http_backend);
    if (cache_backend && cache_backend->destroy)
        cache_backend->destroy(cache_backend);

    ub_server_destroy(server);

    // Destroy observability helpers
    if (logger)
        logger->destroy(logger);
    if (metrics)
        ub_simple_metrics_destroy(metrics);

    // Destroy the top-level scheduler when ownership returns to caller
    mj_scheduler_destroy(&top_sched);

    return rc;
}
