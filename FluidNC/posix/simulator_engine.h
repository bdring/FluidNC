#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Position update type
typedef struct {
    double x;
    double y;
    double z;
    double a;
    double b;
    double c;
} position_update_t;

// Queue message type
typedef struct {
    position_update_t position;
    bool is_final;
} queue_message_t;

// Configure which pins correspond to which axes
// axis_num: 0=X, 1=Y, 2=Z, 3=A, 4=B, 5=C
// step_pin: GPIO pin number that generates step pulses
// dir_pin: GPIO pin number that sets direction
void simulator_set_axis_pins(int axis_num, uint32_t step_pin, uint32_t dir_pin);

// Queue operations - safe to call from ISR
void simulator_queue_position(const position_update_t* pos, bool is_final);
bool simulator_queue_dequeue(queue_message_t* msg);

#ifdef __cplusplus
}
#endif
