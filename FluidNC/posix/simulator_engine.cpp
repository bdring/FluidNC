// Simulator stepping engine - tracks motion and sends position updates via WebSocket

#include "Platform.h"
#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "simulator_engine.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

bool simulator_ws_has_client = false;

void simulator_attach_client() {
    simulator_ws_has_client = true;
}
void simulator_detach_client() {
    simulator_ws_has_client = false;
}

#define STEPS_PER_MM 100

#define UPDATE_US 200000  // 5 updates per second

// FreeRTOS queue for position updates
#define QUEUE_SIZE 100
static QueueHandle_t position_queue = NULL;

// Track pin state and step accumulation during segment
static struct {
    uint32_t step_count[SIMULATOR_MAX_AXES];
    int      direction[SIMULATOR_MAX_AXES];  // -1, 0, or 1
    uint32_t ticks;
    uint32_t duration;
} _segment_state = { 0 };

// Queue Operations (thread-safe, using FreeRTOS queue)

void simulator_queue_position(const position_update_t* pos, bool is_final) {
    // Initialize queue on first use
    if (position_queue == NULL) {
        position_queue = xQueueCreate(QUEUE_SIZE, sizeof(queue_message_t));
        if (position_queue == NULL) {
            fprintf(stderr, "[simulator_engine] Failed to create position queue\n");
            return;
        }
    }

    if (!simulator_ws_has_client) {
        return;
    }

    queue_message_t msg;
    msg.position = *pos;
    msg.is_final = is_final;

    // Send to queue with timeout
    // Provides end-to-end flow control: stepping slows down when WebSocket can't keep up
    BaseType_t result = xQueueSend(position_queue, &msg, pdMS_TO_TICKS(100));
    if (result != pdTRUE) {
        fprintf(stderr, "[simulator_engine] Queue full, dropping position update\n");
    }
}

bool simulator_queue_dequeue(queue_message_t* msg) {
    if (position_queue == NULL) {
        return false;  // Not initialized yet
    }

    // Receive from queue without blocking (called from protocol loop)
    BaseType_t result = xQueueReceive(position_queue, msg, 0);
    return (result == pdTRUE);
}

int simulator_queue_depth(void) {
    if (position_queue == NULL) {
        return 0;
    }

    return uxQueueMessagesWaiting(position_queue);
}

// Helper function to process accumulated steps and send differential step counts
static void flush_segment_position(bool is_final) {
    position_update_t delta     = { 0 };
    bool              has_steps = false;

    // Build step count message with signs applied from direction
    for (int i = 0; i < SIMULATOR_MAX_AXES; i++) {
        int32_t steps = (int32_t)_segment_state.step_count[i];
        if (_segment_state.direction[i] == -1) {
            steps = -steps;
        }
        _segment_state.step_count[i] = 0;  // Reset for next segment
        delta.steps[i]               = steps;
        if (steps != 0) {
            has_steps = true;
        }
    }
    delta.elapsed_us = _segment_state.duration;

    // Only send if there are actual steps or if this is the final flush
    if (has_steps || is_final) {
        if (has_steps) {
            //            fprintf(stderr, "[simulator_engine] FLUSH: X=%d is_final=%d elapsed_us=%u\n",
            //                    delta.steps[0], is_final ? 1 : 0, delta.elapsed_us);
        }
        simulator_queue_position(&delta, is_final);
    }

    _segment_state.duration = 0;
}

// Stepping interface to send messages to a visualizer
// At this level we accumulate pulses and queue a message
// containing step counts.  The message will be handled
// by a task that sends the information to the visualizer,
// perhaps over a websocket.

bool (*_pulse_func)(void);
static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*callback)(void)) {
    frequency   = STEPPING_FREQUENCY;
    _pulse_func = callback;

    // Initialize simulator engine
    memset(_segment_state.step_count, 0, sizeof(_segment_state.step_count));
    memset(_segment_state.direction, 0, sizeof(_segment_state.direction));
    return pulse_delay_us;
}

static uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
    // No initialization needed; step_count will be zero until steps arrive
    return step_pin;
}

static void set_pin(pinnum_t pin, bool level) {
    // gpio_write for actual GPIO (no-op on simulator for now)
}

static void finish_dir() {
    // Record direction values that were set
    // This is called after all set_dir_pin calls have been made
}

static void start_step() {
    // Called when stepping begins
}

// Called with set_step_pin - track which axes are being stepped
static void set_step_pin(pinnum_t pin, bool level) {
    // level=true means step pulse starting
    // pin is the axis number (0=X, 1=Y, 2=Z, 3=A, 4=B, 5=C)
    if (level && pin < SIMULATOR_MAX_AXES) {
        _segment_state.step_count[pin]++;
        if (_segment_state.duration > UPDATE_US) {
            flush_segment_position(false);
        }
    }
}

// Called for direction pins - record the direction for each axis
static void set_dir_pin(pinnum_t pin, bool level) {
    // pin is the axis number (0=X, 1=Y, 2=Z, 3=A, 4=B, 5=C)
    if (pin < SIMULATOR_MAX_AXES) {
        flush_segment_position(false);
        _segment_state.direction[pin] = level ? 1 : -1;
    }
}

static void finish_step() {
    // Called after step pulse is set up
}

static bool start_unstep() {
    // Called to deassert step pins
    return false;
}

static void finish_unstep() {
    // Called after step pins are deasserted
}

static uint32_t max_pulses_per_sec() {
    return 100000;
}

// Called at segment boundaries to mark timer period
static void set_timer_ticks(uint32_t ticks) {
    // This marks a segment boundary - process accumulated steps and send update
    if (_segment_state.duration > UPDATE_US) {
        flush_segment_position(false);
    }

    _segment_state.ticks = ticks;
}

static void start_timer() {
    _segment_state.ticks    = 0;
    _segment_state.duration = 0;

    // Call the pulse function until it returns false
    while (_pulse_func()) {
        _segment_state.duration += _segment_state.ticks;
    }
}

static void stop_timer() {
    // Motion complete - send final position update
    flush_segment_position(true);
}

// Engine Registration

// clang-format off
static step_engine_t engine = {
    "Simulator",
    init_engine,
    init_step_pin,
    set_dir_pin,
    finish_dir,
    start_step,
    set_step_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec,
    set_timer_ticks,
    start_timer,
    stop_timer
};
// clang-format on

#if 0
// Constructor function to register the engine before main() runs
extern "C" {
    __attribute__((constructor)) void __register_Simulator(void) {
        fprintf(stderr, "[simulator_engine] Registering Simulator engine\n");
        simulator_engine.link = step_engines;
        step_engines = &simulator_engine;
        fprintf(stderr, "[simulator_engine] Simulator engine registered, step_engines=%p\n", (void*)step_engines);
    }
}
#endif
REGISTER_STEP_ENGINE(Simulator, &engine);
