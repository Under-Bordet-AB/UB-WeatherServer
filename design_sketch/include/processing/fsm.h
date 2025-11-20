#ifndef UB_FSM_H
#define UB_FSM_H

#include "../core/context.h"

/**
 * @brief State Machine Interface
 * Used to manage the lifecycle of a client connection.
 */

typedef int state_id;

// Common states
#define STATE_INIT 0
#define STATE_ERROR -1
#define STATE_DONE -2

typedef struct fsm fsm;

/**
 * @brief State Handler Function
 * @return The ID of the next state, or current state to stay.
 */
typedef state_id (*state_handler_fn)(fsm* fsm, ub_context* ctx, void* event_data);

/**
 * @brief State Definition
 */
typedef struct {
    state_id id;
    const char* name;
    state_handler_fn on_enter;
    state_handler_fn on_event;
    state_handler_fn on_exit;
} fsm_state;

struct fsm {
    state_id current_state;
    fsm_state* states;
    int num_states;
    void* user_data; // Specific data for the machine (e.g. connection buffer)
};

/**
 * @brief Initialize FSM
 */
void ub_fsm_init(fsm* fsm, fsm_state* states, int num_states, state_id initial_state);

/**
 * @brief Handle an event
 */
void ub_fsm_handle_event(fsm* fsm, ub_context* ctx, void* event_data);

#endif // UB_FSM_H
