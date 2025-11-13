#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_TASKS 1000

typedef struct mj_scheduler mj_scheduler;

// Task CREATE callback prototype
typedef void (*mj_task_create_fn)(mj_scheduler* scheduler, void* user_data);
// Task RUN callback prototype
typedef void (*mj_task_run_fn)(mj_scheduler* scheduler, void* user_data);
// Task DESTROY callback prototype
typedef void (*mj_task_destroy_fn)(mj_scheduler* scheduler, void* user_data);

typedef struct mj_task {
    mj_task_create_fn create; // optional factory
    mj_task_run_fn run;
    mj_task_destroy_fn destroy; // optional cleanup
    void* user_data;            // opaque data owned by the SM
} mj_task;

mj_scheduler* mj_scheduler_create();
int mj_scheduler_destroy(mj_scheduler** scheduler);

int mj_scheduler_run(mj_scheduler* scheduler);

// return -1 if task_list[] is full
int mj_scheduler_task_add(mj_scheduler* scheduler, mj_task* task, void* user_data);

// Only usable from within a task callback, removes the current task.
int mj_scheduler_task_remove_current(mj_scheduler* scheduler);