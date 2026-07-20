#pragma once

#include <stdbool.h>
#include <stdint.h>

// Maximum number of axes (matches FluidNC config)
#define SIMULATOR_MAX_AXES 6

// Differential step update type (not absolute position)
// Each field is the signed step count change for this segment
// Index: 0=X, 1=Y, 2=Z, 3=A, 4=B, 5=C (up to SIMULATOR_MAX_AXES)
typedef struct {
    int32_t  steps[SIMULATOR_MAX_AXES];
    uint32_t elapsed_us;
} position_update_t;

// Queue message type
typedef struct {
    position_update_t position;
    bool is_final;
} queue_message_t;

void simulator_attach_client();
void simulator_detach_client();

// Queue operations - safe to call from ISR
void simulator_queue_position(const position_update_t* pos, bool is_final);
bool simulator_queue_dequeue(queue_message_t* msg);

// Get current queue depth for flow control
int simulator_queue_depth(void);
