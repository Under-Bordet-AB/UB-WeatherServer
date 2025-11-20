#include "../../include/processing/fsm.h"
#include <stdio.h>

void ub_fsm_init(fsm* fsm, fsm_state* states, int num_states, state_id initial_state) {
    fsm->states = states;
    fsm->num_states = num_states;
    fsm->current_state = initial_state;

    // Trigger enter on initial state if it exists
    // Note: Usually we might want to wait for start(), but for simplicity:
    for (int i = 0; i < num_states; i++) {
        if (states[i].id == initial_state) {
            if (states[i].on_enter) {
                // We don't have event data for init, pass NULL
                states[i].on_enter(fsm, NULL, NULL);
            }
            break;
        }
    }
}

static fsm_state* find_state(fsm* fsm, state_id id) {
    for (int i = 0; i < fsm->num_states; i++) {
        if (fsm->states[i].id == id) {
            return &fsm->states[i];
        }
    }
    return NULL;
}

void ub_fsm_handle_event(fsm* fsm, ub_context* ctx, void* event_data) {
    fsm_state* current = find_state(fsm, fsm->current_state);
    if (!current)
        return;

    state_id next_state_id = fsm->current_state;

    // 1. Handle Event
    if (current->on_event) {
        next_state_id = current->on_event(fsm, ctx, event_data);
    }

    // 2. Transition if needed
    if (next_state_id != fsm->current_state) {
        // Exit old
        if (current->on_exit) {
            current->on_exit(fsm, ctx, event_data);
        }

        // Update state
        fsm->current_state = next_state_id;
        fsm_state* next = find_state(fsm, next_state_id);

        // Enter new
        if (next && next->on_enter) {
            // The enter handler might immediately trigger another transition
            // In a real system, we might loop here, but be careful of infinite loops
            state_id immediate_next = next->on_enter(fsm, ctx, event_data);
            if (immediate_next != next_state_id) {
                // Recursive call or loop to handle immediate transition
                // For now, let's just update the state ID so the next event sees it
                // But ideally we should process the chain.
                fsm->current_state = immediate_next;
                // Note: This simple implementation doesn't recursively call enter/exit for the immediate jump
                // A robust implementation would use a loop.
            }
        }
    }
}
