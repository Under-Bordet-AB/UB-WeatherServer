#include "majjen.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct mj_scheduler {
    mj_task* task_list[MAX_TASKS];
    mj_task** current_task; // double pointer, so we dont have to search array to remove task
    size_t task_count;
} mj_scheduler;

int mj_scheduler_run(mj_scheduler* scheduler) {
    if (scheduler == NULL) {
        errno = EINVAL;
        return -1;
    }
    mj_task** current_task_slot = NULL;
    mj_task* current_task = NULL;

    while (scheduler->task_count > 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            current_task_slot = &scheduler->task_list[i];
            current_task = *current_task_slot;
            // Skip to next if no task in slot
            if (current_task == NULL) {
                continue;
            }

            // Skip if task has no run function
            if (current_task->run == NULL) {
                continue;
            }

            scheduler->current_task = current_task_slot; // Note: double pointers

            // Call tasks run function with its context
            current_task->run(scheduler, current_task->ctx);

            // Reset current function since it should only be available from the task that just ran
            scheduler->current_task = NULL;
        }
    }
    // All tasks completed
    return 0;
}

mj_scheduler* mj_scheduler_create(void) {
    mj_scheduler* scheduler = calloc(1, sizeof(*scheduler));
    if (!scheduler) {
        errno = ENOMEM;
        return NULL;
    }

    scheduler->current_task = NULL;
    scheduler->task_count = 0;

    return scheduler;
}

int mj_scheduler_task_add(mj_scheduler* scheduler, mj_task* new_task) {
    if (scheduler == NULL || new_task == NULL) {
        errno = EINVAL;
        return -1;
    }
    // if scheduler is full
    if (scheduler->task_count >= MAX_TASKS) {
        errno = ENOMEM;
        return -1;
    }

    // Add task to first empty slot in task_list[]
    for (int i = 0; i < MAX_TASKS; i++) {
        if (scheduler->task_list[i] == NULL) {
            scheduler->task_list[i] = new_task;
            scheduler->task_count++;
            return 0;
        }
    }
    // Should never happen
    errno = ENOMEM;
    return -1;
}

// Calls the tasks cleanup function and then it frees the task
int mj_scheduler_task_remove_current(mj_scheduler* scheduler) {
    if (scheduler == NULL || scheduler->current_task == NULL || *scheduler->current_task == NULL) {
        errno = EINVAL;
        return -1;
    }

    // helper stack alias for shorter code
    mj_task* task = *scheduler->current_task;

    // use custom cleanup for any internal data if availible
    if (task->cleanup && task->ctx) {
        task->cleanup(scheduler, task->ctx);
    }

    // Free the tasks context
    free(task->ctx);
    task->ctx = NULL;

    // Free the task itself
    free(task);
    task = NULL;

    // Clear the slot in scheduler->task_list
    *scheduler->current_task = NULL;

    // Clear the current_task pointer
    scheduler->current_task = NULL;

    // Decrement task count
    if (scheduler->task_count > 0)
        scheduler->task_count--;

    return 0;
}

int mj_scheduler_destroy(mj_scheduler** scheduler) {
    if (scheduler == NULL || *scheduler == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Don't cleanup if tasks are left
    if ((*scheduler)->task_count > 0) {
        errno = EBUSY; // resource busy
        return 1;
    }

    free(*scheduler);

    // Clear caller's pointer to avoid dangling references, this is why we use double pointers
    *scheduler = NULL;

    return 0;
}
