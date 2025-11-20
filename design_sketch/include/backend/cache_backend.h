#ifndef UB_CACHE_BACKEND_H
#define UB_CACHE_BACKEND_H

#include "backend_interface.h"

// Factory for a simple in-memory cache backend. Fetches are delivered
// asynchronously (via the scheduler) even when a cache hit occurs so
// callers observe uniform async semantics.

// Create a cache backend instance. `scheduler` is required and used to
// schedule async callback tasks. `max_entries` is a soft limit; this
// implementation will not enforce strict eviction policy in the sketch.
backend* ub_cache_backend_create(struct mj_scheduler* scheduler, size_t max_entries);

#endif // UB_CACHE_BACKEND_H
