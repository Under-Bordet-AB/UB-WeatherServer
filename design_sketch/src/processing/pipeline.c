#include "../../include/processing/pipeline.h"
#include <stdlib.h>

#define MAX_FILTERS 16

struct pipeline {
    filter* filters[MAX_FILTERS];
    int count;
};
pipeline* ub_pipeline_create(void) {
    pipeline* p = malloc(sizeof(pipeline));
    if (p) {
        p->count = 0;
    }
    return p;
}

void ub_pipeline_add_filter(pipeline* pipeline, filter* filter) {
    if (pipeline->count < MAX_FILTERS) {
        pipeline->filters[pipeline->count++] = filter;
    }
}

filter_status ub_pipeline_execute(pipeline* pipeline, ub_context* ctx, void* input, void** output) {
    void* current_data = input;
    void* next_data = NULL;
    filter_status status = FILTER_CONTINUE;

    for (int i = 0; i < pipeline->count; i++) {
        filter* f = pipeline->filters[i];
        status = f->process(f, ctx, current_data, &next_data);

        if (status == FILTER_STOP || status == FILTER_WAIT) {
            *output = next_data; // Return whatever we have so far
            return status;
        }

        // Output of this filter becomes input of next
        current_data = next_data;
    }

    *output = current_data;
    return FILTER_CONTINUE;
}

void ub_pipeline_destroy(pipeline* pipeline) {
    if (!pipeline)
        return;
    for (int i = 0; i < pipeline->count; i++) {
        if (pipeline->filters[i]->destroy) {
            pipeline->filters[i]->destroy(pipeline->filters[i]);
        }
    }
    free(pipeline);
}
