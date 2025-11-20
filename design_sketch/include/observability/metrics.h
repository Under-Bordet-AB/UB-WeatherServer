#ifndef UB_METRICS_H
#define UB_METRICS_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Metric types supported by the system
 */
typedef enum {
    METRIC_COUNTER,
    METRIC_GAUGE,
    METRIC_HISTOGRAM
} metric_type;

/**
 * @brief Opaque handle for a metric recorder
 */
typedef struct metrics_recorder metrics_recorder;

/**
 * @brief Interface for recording metrics.
 * This allows for different implementations (Prometheus, StatsD, Log-based)
 * to be swapped in without changing application code.
 */
typedef struct {
    void (*inc_counter)(metrics_recorder* recorder, const char* name, const char* labels[]);
    void (*set_gauge)(metrics_recorder* recorder, const char* name, double value, const char* labels[]);
    void (*observe_histogram)(metrics_recorder* recorder, const char* name, double value, const char* labels[]);
} metrics_interface;

// Global access to the configured metrics implementation
extern const metrics_interface* g_metrics;
extern metrics_recorder* g_recorder;

// Helpers to create a simple in-memory recorder for tests and examples.
metrics_recorder* ub_simple_metrics_create(void);
void ub_simple_metrics_destroy(metrics_recorder* r);

#endif // UB_METRICS_H
