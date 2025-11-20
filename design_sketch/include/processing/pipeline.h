#ifndef UB_PIPELINE_H
#define UB_PIPELINE_H

#include "filter.h"

typedef struct pipeline pipeline;

/**
 * @brief Create a new processing pipeline
 */
pipeline* ub_pipeline_create(void);

/**
 * @brief Add a filter to the end of the pipeline
 */
void ub_pipeline_add_filter(pipeline* pipeline, filter* filter);

/**
 * @brief Execute the pipeline
 * @return Final status
 */
filter_status ub_pipeline_execute(pipeline* pipeline, ub_context* ctx, void* input, void** output);

void ub_pipeline_destroy(pipeline* pipeline);

#endif // UB_PIPELINE_H
