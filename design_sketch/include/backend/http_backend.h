#ifndef UB_HTTP_BACKEND_H
#define UB_HTTP_BACKEND_H

#include "../../core/context.h"
#include "backend_interface.h"

// Factory for an HTTP-like backend that simulates async HTTP requests by
// scheduling tasks on the provided mj_scheduler. The returned backend must
// be destroyed with its ->destroy method.

// Create an HTTP backend instance. `scheduler` is required and used to
// schedule async fetch tasks. `base_url` is stored by the backend for
// informational/templating purposes.
backend* ub_http_backend_create(struct mj_scheduler* scheduler, const char* base_url);

#endif // UB_HTTP_BACKEND_H
