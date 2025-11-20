#include "../../include/observability/metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct metrics_recorder {
    struct metric_node* head;
};

typedef struct metric_node {
    char* name;
    double value;
    struct metric_node* next;
} metric_node;

static void metrics_inc_counter(metrics_recorder* recorder, const char* name, const char* labels[]) {
    (void)labels;
    if (!recorder || !name)
        return;
    metric_node* cur = recorder->head;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            cur->value += 1.0;
            return;
        }
        cur = cur->next;
    }
    metric_node* n = malloc(sizeof(metric_node));
    n->name = strdup(name);
    n->value = 1.0;
    n->next = recorder->head;
    recorder->head = n;
}

static void metrics_set_gauge(metrics_recorder* recorder, const char* name, double value, const char* labels[]) {
    (void)labels;
    if (!recorder || !name)
        return;
    metric_node* cur = recorder->head;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            cur->value = value;
            return;
        }
        cur = cur->next;
    }
    metric_node* n = malloc(sizeof(metric_node));
    n->name = strdup(name);
    n->value = value;
    n->next = recorder->head;
    recorder->head = n;
}

static void
metrics_observe_histogram(metrics_recorder* recorder, const char* name, double value, const char* labels[]) {
    (void)recorder;
    (void)name;
    (void)value;
    (void)labels;
    // Not implemented in simple recorder
}

static const metrics_interface simple_interface = {.inc_counter = metrics_inc_counter,
                                                   .set_gauge = metrics_set_gauge,
                                                   .observe_histogram = metrics_observe_histogram};

const metrics_interface* g_metrics = &simple_interface;
metrics_recorder* g_recorder = NULL;

metrics_recorder* ub_simple_metrics_create(void) {
    metrics_recorder* r = calloc(1, sizeof(metrics_recorder));
    if (!r)
        return NULL;
    r->head = NULL;
    g_recorder = r;
    g_metrics = &simple_interface;
    return r;
}

void ub_simple_metrics_destroy(metrics_recorder* r) {
    if (!r)
        return;
    metric_node* cur = r->head;
    while (cur) {
        metric_node* next = cur->next;
        free(cur->name);
        free(cur);
        cur = next;
    }
    if (g_recorder == r)
        g_recorder = NULL;
    g_metrics = &simple_interface; // leave interface pointer valid
    free(r);
}
