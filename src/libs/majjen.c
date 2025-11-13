#include "majjen.h"
#include <errno.h>
#include <stdlib.h>
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

    while (scheduler->task_count > 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            // Skip to next if no task in slot
            if (scheduler->task_list[i] == NULL) {
                continue;
            }

            // Keep track of what function is running
            scheduler->current_task = &scheduler->task_list[i];

            // Call user function with user user_data
            scheduler->task_list[i]->run(scheduler, scheduler->task_list[i]->user_data);

            // Reset current function since it should only be availible from the task that just ran
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

    // init all values
    scheduler->current_task = NULL;
    scheduler->task_count = 0;

    // TODO if we stick with fixed array preallocate all slots here during init
    return scheduler;
}

int mj_scheduler_task_add(mj_scheduler* scheduler, mj_task* task, void* user_data) {
    if (scheduler == NULL || task == NULL) {
        errno = EINVAL;
        return -1;
    }
    // Scheduler is full
    if (scheduler->task_count >= MAX_TASKS) {
        errno = ENOMEM;
        return -1;
    }
    // Create the new task
    mj_task* new_task = malloc(sizeof(*new_task));
    if (!new_task) {
        errno = ENOMEM;
        return -1;
    }
    new_task->create = task->create;
    new_task->run = task->run;
    new_task->destroy = task->destroy;
    new_task->user_data = task->user_data;
    // Add task in empty spot in task_list[] (must loop entire array since list will be fragmented, implement min-heap
    // to fix)
    for (int i = 0; i < MAX_TASKS; i++) {
        if (scheduler->task_list[i] == NULL) {
            scheduler->task_list[i] = new_task;
            scheduler->task_count++;
            return 0;
        }
    }
    // Should never happen
    free(new_task);
    errno = ENOMEM;
    return -1;
}

// TODO i think i must pass in a destructor / remove funciton also?
int mj_scheduler_task_remove_current(mj_scheduler* scheduler) {
    if (scheduler == NULL || scheduler->current_task == NULL || *scheduler->current_task == NULL) {
        errno = EINVAL;
        return -1;
    }

    mj_task* task = *scheduler->current_task;

    // Free user_data first (the user_data comes from the callbacks instance,
    // this is OK since the instance scope is lost after task is done)
    if (task->user_data) {
        free(task->user_data);
        task->user_data = NULL;
    }

    // Free the task struct itself
    free(task);

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

    // Don't destroy if tasks are left
    if ((*scheduler)->task_count > 0) {
        errno = EBUSY; // resource busy
        return 1;
    }

    free(*scheduler);

    // Clear caller's pointer to avoid dangling references, this is why we use double pointers
    *scheduler = NULL;

    return 0;
}
