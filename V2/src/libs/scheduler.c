/*
 * scheduler.c
 * Minimal cooperative scheduler scaffold. Functions are stubs with TODOs
 * — intended as scaffolding so the app can plug in real behavior.
 */

#include "../libs/scheduler.h"
#include <stdio.h>
#include <stdlib.h>

/* Simple singly-linked list of tasks for the scaffold. Real impl may use
 * a more sophisticated queue and timeouts.
 */
static task_t *task_list = NULL;

task_t *scheduler_create_task(task_fn_t fn, void *ctx) {
  if (!fn)
    return NULL;
  task_t *t = calloc(1, sizeof(task_t));
  if (!t)
    return NULL;
  t->fn = fn;
  t->ctx = ctx;
  t->status = task_status_ready;
  /* TODO: insert into task_list */
  t->user = NULL;
  return t;
}

void scheduler_destroy_task(task_t *t) {
  if (!t)
    return;
  /* TODO: remove from task_list if present */
  free(t);
}

size_t scheduler_run_once(void) {
  /* TODO: iterate ready tasks and run them. This is a placeholder that
   * demonstrates the intended control flow; real implementation must
   * traverse the task list and call task->fn(task, task->ctx).
   */
  (void)task_list;
  /* nothing executed in scaffold */
  return 0;
}

void scheduler_run_loop(void) {
  /* TODO: call scheduler_run_once in a loop and sleep/yield as needed.
   * Provide a way to break out (signal, callback, or return value).
   */
  while (0) {
    size_t ran = scheduler_run_once();
    (void)ran;
    break; /* scaffold stops immediately */
  }
}

void task_yield(task_t *t) {
  if (!t)
    return;
  /* TODO: set t->status appropriately and return control to scheduler */
  t->status = task_status_waiting;
}

void task_set_waiting(task_t *t) {
  if (!t)
    return;
  t->status = task_status_waiting;
}

void task_set_ready(task_t *t) {
  if (!t)
    return;
  t->status = task_status_ready;
}
