#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_TASKS 5

typedef struct mj_scheduler mj_scheduler;

// Task function prototype
typedef void (*mj_task_fn)(mj_scheduler* scheduler, void* ctx);

typedef struct mj_task {
    // NOTE mj_task_fn is a pointer, look at above declaration
    mj_task_fn create; // optional factory for any internally allocated data
    mj_task_fn run;
    mj_task_fn cleanup; // optional cleanup for any internally allocated data
    void* ctx;
} mj_task;

mj_scheduler* mj_scheduler_create();
int mj_scheduler_destroy(mj_scheduler** scheduler);

int mj_scheduler_run(mj_scheduler* scheduler);

// return -1 if task_list[] is full
int mj_scheduler_task_add(mj_scheduler* scheduler, mj_task* task);

// Only usable from within a task callback, removes the current task.
int mj_scheduler_task_remove_current(mj_scheduler* scheduler);

size_t mj_scheduler_update_highest_fd(mj_scheduler* scheduler, int fd);