#ifndef UB_BUSINESS_FILTER_H
#define UB_BUSINESS_FILTER_H

#include "../backend/backend_interface.h"
#include "pipeline.h"

// Create a business logic filter that queries a backend asynchronously.
// The filter returns FILTER_WAIT and the backend callback will resume the
// connection's FSM and populate the response.
filter* create_business_filter(backend* backend);

#endif // UB_BUSINESS_FILTER_H
