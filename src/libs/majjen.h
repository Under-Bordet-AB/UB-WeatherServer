#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_TASKS 1000

// Forward declarations of opaque structs
typedef struct mj_task mj_task;
typedef struct mj_scheduler mj_scheduler;

// Task callback prototype
typedef void(mj_task_fn)(mj_scheduler* scheduler, void* state);

/*
    Create / Destroy
*/

// Create a scheduler. Allocates memory for the scheduler struct.
// returns NULL error.
mj_scheduler* mj_scheduler_create();
int mj_scheduler_destroy(mj_scheduler** scheduler);

/*
    Scheduler management
*/

// Start scheduler. Returns when no tasks are left.
int mj_scheduler_run(mj_scheduler* scheduler);

/*
    Task management
*/

// Add a task. Mallocs a new mj_task.
// return -1 if task_list[] is full
int mj_scheduler_task_add(mj_scheduler* scheduler, mj_task_fn* task_fn, void* user_state);

// Only usable from within a task callback, removes the current task.
int mj_scheduler_task_remove_current(mj_scheduler* scheduler);