/*
 * scheduler.h
 * Cooperative (yielding) scheduler scaffold.
 * Minimal API and types. Implementation is in scheduler.c.
 */
#ifndef v2_src_libs_scheduler_h
#define v2_src_libs_scheduler_h

#include <stddef.h>
#include <stdint.h>

typedef enum {
  task_status_ready = 0,
  task_status_running,
  task_status_waiting,
  task_status_done,
} task_status_t;

typedef struct task_t task_t;

/* Task function callback: should return a task_status_t indicating status after
 * running. Example: yield by returning task_status_waiting and registering wake
 * conditions.
 */
typedef task_status_t (*task_fn_t)(task_t *task, void *ctx);

struct task_t {
  task_fn_t fn;
  void *ctx;
  task_status_t status;
  /* user data / small state may go here */
  void *user;
};

/* API */
task_t *scheduler_create_task(task_fn_t fn, void *ctx);
void scheduler_destroy_task(task_t *t);

/* Run pending tasks once. Returns number of tasks executed. */
size_t scheduler_run_once(void);

/* Run loop until no ready tasks or external stop. Blocks. */
void scheduler_run_loop(void);

/* Helpers for tasks to yield / set status. */
void task_yield(task_t *t);
void task_set_waiting(task_t *t);
void task_set_ready(task_t *t);

#endif /* v2_src_libs_scheduler_h */
