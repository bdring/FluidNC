// Simulator stepping engine - tracks motion and sends position updates via WebSocket

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include <string.h>
#include <stdio.h>

// Forward declaration from SimulatorWebSocketServer
extern "C" {
    bool simulator_ws_has_client(void);
}

#define MAX_AXES 6  // X, Y, Z, A, B, C
#define STEPS_PER_MM 100

// Position update message to be sent via WebSocket
typedef struct {
    double x;
    double y;
    double z;
    double a;
    double b;
    double c;
} position_update_t;

// Queue message structure
typedef struct {
    position_update_t position;
    bool is_final;  // true if this is the final position for a job
} queue_message_t;

// Simple queue implementation
#define QUEUE_SIZE 64
static queue_message_t message_queue[QUEUE_SIZE];
static volatile int queue_head = 0;
static volatile int queue_tail = 0;
static volatile int queue_count = 0;

// Track pin state and step accumulation during segment
static struct {
    uint32_t step_count[MAX_AXES];
    int      direction[MAX_AXES];  // -1, 0, or 1
    bool     pin_used[MAX_AXES];
} _segment_state = {0};

// Map pin numbers to axes (will be initialized based on machine config)
// For now, use a simple mapping that can be updated later
static pinnum_t axis_step_pins[MAX_AXES] = {0};
static pinnum_t axis_dir_pins[MAX_AXES] = {0};
static int      num_axes = 3;  // Default to X, Y, Z

// Current position
static double _current_pos[MAX_AXES] = {0};

// ============================================================================
// Queue Operations (thread-safe for ISR)
// ============================================================================

extern "C" {

void simulator_queue_position(const position_update_t* pos, bool is_final) {
    static bool last_client_state = false;
    
    // Don't queue if there's no client connected
    bool has_client = simulator_ws_has_client();
    
    if (has_client != last_client_state) {
        last_client_state = has_client;
        // State change already logged by simulator_ws_has_client()
    }
    
    if (!has_client) {
        return;
    }

    if (queue_count >= QUEUE_SIZE) {
        // Queue full, drop oldest message
        fprintf(stderr, "[simulator_engine] Queue full! Dropping oldest message. count=%u\n", queue_count);
        queue_head = (queue_head + 1) % QUEUE_SIZE;
        queue_count--;
    }

    message_queue[queue_tail].position = *pos;
    message_queue[queue_tail].is_final = is_final;
    queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    queue_count++;
}

bool simulator_queue_dequeue(queue_message_t* msg) {
    if (queue_count == 0) {
        return false;
    }

    *msg = message_queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_SIZE;
    queue_count--;

    return true;
}

}  // extern "C"

// ============================================================================
// Configure axis pin mapping
// ============================================================================

extern "C" {

void simulator_set_axis_pins(int axis_num, pinnum_t step_pin, pinnum_t dir_pin) {
    if (axis_num < MAX_AXES) {
        axis_step_pins[axis_num] = step_pin;
        axis_dir_pins[axis_num] = dir_pin;
        if (axis_num >= num_axes) {
            num_axes = axis_num + 1;
        }
    }
}

}  // extern "C"

// ============================================================================
// Step Engine Interface Implementation
// ============================================================================

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t frequency, bool (*callback)(void)) {
    // Initialize simulator engine
    memset(_segment_state.step_count, 0, sizeof(_segment_state.step_count));
    memset(_segment_state.direction, 0, sizeof(_segment_state.direction));
    memset(_segment_state.pin_used, 0, sizeof(_segment_state.pin_used));
    return pulse_delay_us;
}

static uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
    // Register this as a step pin if it's one we recognize
    for (int i = 0; i < num_axes; i++) {
        if (axis_step_pins[i] == step_pin) {
            _segment_state.pin_used[i] = true;
            return step_pin;
        }
    }
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
    // Count step edges (on rising edge, false->true)
    // level=true means step pulse starting
    if (level) {
        for (int i = 0; i < num_axes; i++) {
            if (axis_step_pins[i] == pin && _segment_state.pin_used[i]) {
                _segment_state.step_count[i]++;
                break;
            }
        }
    }
}

// Called for direction pins - record the direction for each axis
static void set_dir_pin(pinnum_t pin, bool level) {
    for (int i = 0; i < num_axes; i++) {
        if (axis_dir_pins[i] == pin) {
            _segment_state.direction[i] = level ? 1 : -1;
            break;
        }
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

// Helper function to process accumulated steps and send position update
static void flush_segment_position(bool is_final) {
    position_update_t pos = {0};
    
    // Process accumulated steps for all axes
    for (int i = 0; i < num_axes; i++) {
        if (_segment_state.step_count[i] > 0) {
            double distance = (double)_segment_state.step_count[i] / STEPS_PER_MM;
            if (_segment_state.direction[i] == -1) {
                distance = -distance;
            }
            _current_pos[i] += distance;
            _segment_state.step_count[i] = 0;
        }
    }

    // Build position update message
    pos.x = _current_pos[0];
    pos.y = _current_pos[1];
    pos.z = _current_pos[2];
    if (num_axes > 3) pos.a = _current_pos[3];
    if (num_axes > 4) pos.b = _current_pos[4];
    if (num_axes > 5) pos.c = _current_pos[5];

    simulator_queue_position(&pos, is_final);
}

// Called at segment boundaries to mark timer period
static void set_timer_ticks(uint32_t ticks) {
    // This marks a segment boundary - process accumulated steps and send update
    flush_segment_position(false);
}

static void start_timer() {
    // Timer started - prepare for stepping
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
