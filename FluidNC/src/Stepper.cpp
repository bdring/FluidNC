// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Stepper.cpp - stepper motor driver: executes motion plans using stepper motors
*/

#include "Stepper.h"

#include "Machine/MachineConfig.h"
#include "MotionControl.h"
#include "Stepping.h"
#include "StepperPrivate.h"
#include "Planner.h"
#include "Protocol.h"
#include <esp_attr.h>  // IRAM_ATTR
#include <cmath>

using namespace Stepper;

#ifdef DEBUG_STEPPING
uint32_t st_seq = 0;
uint32_t st_seq0;
uint32_t seg_seq0 = 0;
uint32_t seg_seq1 = 0;
uint32_t seg_seq_act;
uint32_t seg_seq_exp;
uint32_t pl_seq0;
#endif

// Stores the planner block Bresenham algorithm execution data for the segments in the segment
// buffer. Normally, this buffer is partially in-use, but, for the worst case scenario, it will
// never exceed the number of accessible stepper buffer segments (SEGMENT_BUFFER_SIZE-1).
// NOTE: This data is copied from the prepped planner blocks so that the planner blocks may be
// discarded when entirely consumed and completed by the segment buffer. Also, AMASS alters this
// data for its own use.
struct st_block_t {
    uint32_t steps[MAX_N_AXIS];
    uint32_t step_event_count;
    uint8_t  direction_bits;
    bool     is_pwm_rate_adjusted;  // Tracks motions that require constant laser power/rate
#ifdef DEBUG_STEPPING
    uint32_t entry[MAX_N_AXIS];
    //    uint32_t exit[MAX_N_AXIS];
#endif
};
static volatile st_block_t st_block_buffer[SEGMENT_BUFFER_SIZE - 1];

// Primary stepper segment ring buffer. Contains small, short line segments for the stepper
// algorithm to execute, which are "checked-out" incrementally from the first block in the
// planner buffer. Once "checked-out", the steps in the segments buffer cannot be modified by
// the planner, where the remaining planner block steps still can.
struct segment_t {
#ifdef DEBUG_STEPPING
    uint32_t seq;
#endif
    uint16_t     n_step;             // Number of step events to be executed for this segment
    uint16_t     isrPeriod;          // Time to next ISR tick, in units of timer ticks
    uint8_t      st_block_index;     // Stepper block data index. Uses this information to execute this segment.
    uint8_t      amass_level;        // AMASS level for the ISR to execute this segment
    uint16_t     spindle_dev_speed;  // Spindle speed scaled to the device
    SpindleSpeed spindle_speed;      // Spindle speed in GCode units
};
static segment_t segment_buffer[SEGMENT_BUFFER_SIZE];

// Stepper ISR data struct. Contains the running data for the main stepper ISR.
typedef struct {
    // Used by the bresenham line algorithm

    uint32_t counter[MAX_N_AXIS];  // Counter variables for the bresenham line tracer

    uint8_t  step_bits;     // Stores out_bits output to complete the step pulse delay
    uint8_t  execute_step;  // Flags step execution for each interrupt.
    uint8_t  step_outbits;  // The next stepping-bits to be output
    uint8_t  dir_outbits;
    uint32_t steps[MAX_N_AXIS];

    uint16_t             step_count;        // Steps remaining in line segment motion
    uint8_t              exec_block_index;  // Tracks the current st_block index. Change indicates new block.
    volatile st_block_t* exec_block;        // Pointer to the block data for the segment being executed
    volatile segment_t*  exec_segment;      // Pointer to the segment being executed
} stepper_t;
static stepper_t st;

// Step segment ring buffer indices
static volatile uint8_t segment_buffer_tail;
static volatile uint8_t segment_buffer_head;
static uint8_t          segment_next_head;

// Pointers for the step segment being prepped from the planner buffer. Accessed only by the
// main program. Pointers may be planning segments or planner blocks ahead of what being executed.
static plan_block_t*        pl_block;       // Pointer to the planner block being prepped
static volatile st_block_t* st_prep_block;  // Pointer to the stepper block data being prepped

// Segment preparation data struct. Contains all the necessary information to compute new segments
// based on the current executing planner block.
typedef struct {
    uint8_t  st_block_index;  // Index of stepper common data block being prepped
    PrepFlag recalculate_flag;

    float dt_remainder;
    float steps_remaining;
    float step_per_mm;
    float req_mm_increment;

    uint8_t last_st_block_index;
    float   last_steps_remaining;
    float   last_step_per_mm;
    float   last_dt_remainder;

    uint8_t ramp_type;    // Current segment ramp state
    float   mm_complete;  // End of velocity profile from end of current planner block in (mm).
    // NOTE: This value must coincide with a step(no mantissa) when converted.
    float current_speed;     // Current speed at the end of the segment buffer (mm/min)
    float maximum_speed;     // Maximum speed of executing block. Not always nominal speed. (mm/min)
    float exit_speed;        // Exit speed of executing block (mm/min)
    float accelerate_until;  // Acceleration ramp end measured from end of block (mm)
    float decelerate_after;  // Deceleration ramp start measured from end of block (mm)

    float        inv_rate;  // Used by PWM laser mode to speed up segment calculations.
    SpindleSpeed current_spindle_speed;

} st_prep_t;
static st_prep_t prep;

/* "The Stepper Driver Interrupt" - This timer interrupt is the workhorse, employing
   the venerable Bresenham line algorithm to manage and exactly synchronize multi-axis moves.
   Unlike the popular DDA algorithm, the Bresenham algorithm is not susceptible to numerical
   round-off errors and only requires fast integer counters, meaning low computational overhead
   and maximizing the Arduino's capabilities. However, the downside of the Bresenham algorithm
   is, for certain multi-axis motions, the non-dominant axes may suffer from un-smooth step
   pulse trains, or aliasing, which can lead to strange audible noises or shaking. This is
   particularly noticeable or may cause motion issues at low step frequencies (0-5kHz), but
   is usually not a physical problem at higher frequencies, although audible.
     To improve Bresenham multi-axis performance, we use what we call an Adaptive Multi-Axis
   Step Smoothing (AMASS) algorithm, which does what the name implies. At lower step frequencies,
   AMASS artificially increases the Bresenham resolution without effecting the algorithm's
   innate exactness. AMASS adapts its resolution levels automatically depending on the step
   frequency to be executed, meaning that for even lower step frequencies the step smoothing
   level increases. Algorithmically, AMASS is acheived by a simple bit-shifting of the Bresenham
   step count for each AMASS level. For example, for a Level 1 step smoothing, we bit shift
   the Bresenham step event count, effectively multiplying it by 2, while the axis step counts
   remain the same, and then double the stepper ISR frequency. In effect, we are allowing the
   non-dominant Bresenham axes step in the intermediate ISR tick, while the dominant axis is
   stepping every two ISR ticks, rather than every ISR tick in the traditional sense. At AMASS
   Level 2, we simply bit-shift again, so the non-dominant Bresenham axes can step within any
   of the four ISR ticks, the dominant axis steps every four ISR ticks, and quadruple the
   stepper ISR frequency. And so on. This, in effect, virtually eliminates multi-axis aliasing
   issues with the Bresenham algorithm and does not significantly alter the performance, but
   in fact, more efficiently utilizes unused CPU cycles overall throughout all configurations.
     AMASS retains the Bresenham algorithm exactness by requiring that it always executes a full
   Bresenham step, regardless of AMASS Level. Meaning that for an AMASS Level 2, all four
   intermediate steps must be completed such that baseline Bresenham (Level 0) count is always
   retained. Similarly, AMASS Level 3 means all eight intermediate steps must be executed.
   Although the AMASS Levels are in reality arbitrary, where the baseline Bresenham counts can
   be multiplied by any integer value, multiplication by powers of two are simply used to ease
   CPU overhead with bitshift integer operations.
     This interrupt is simple and dumb by design. All the computational heavy-lifting, as in
   determining accelerations, is performed elsewhere. This interrupt pops pre-computed segments,
   defined as constant velocity over n number of steps, from the step segment buffer and then
   executes them by pulsing the stepper pins appropriately via the Bresenham algorithm. This
   ISR is supported by The Stepper Port Reset Interrupt which it uses to reset the stepper port
   after each pulse. The bresenham line tracer algorithm controls all stepper outputs
   simultaneously with these two interrupts.

   NOTE: This interrupt must be as efficient as possible and complete before the next ISR tick.
   NOTE: This ISR expects at least one step to be executed per segment.

	 The complete step timing should look this...
		Direction pin is set
		An optional delay (direction_delay_microseconds) is put after this
		The step pin is started
		A pulse length is determine (via option $0 ... pulse_microseconds)
		The pulse is ended
		Direction will remain the same until another step occurs with a change in direction.


*/

// Stepper shutdown
static void IRAM_ATTR stop_stepping() {
    // Disable Stepping Driver Interrupt.
    config->_stepping->stopTimer();
    config->_axes->unstep();
    st.step_outbits = 0;
}

/**
 * This phase of the ISR should ONLY create the pulses for the steppers.
 * This prevents jitter caused by the interval between the start of the
 * interrupt and the start of the pulses. DON'T add any logic ahead of the
 * call to this method that might cause variation in the timing. The aim
 * is to keep pulse timing as regular as possible.
 */
void IRAM_ATTR Stepper::pulse_func() {
    auto n_axis = config->_axes->_numberAxis;

    config->_axes->step(st.step_outbits, st.dir_outbits);

    // If there is no step segment, attempt to pop one from the stepper buffer
    if (st.exec_segment == NULL) {
        // Anything in the buffer? If so, load and initialize next step segment.
        if (segment_buffer_head != segment_buffer_tail) {
            // Initialize new step segment and load number of steps to execute
            st.exec_segment = &segment_buffer[segment_buffer_tail];
#ifdef DEBUG_STEPPING
            if (st.exec_segment->seq != seg_seq1) {
                seg_seq_act = st.exec_segment->seq;
                seg_seq_exp = seg_seq1;
                rtSegSeq    = true;
            }
            seg_seq1++;
#endif
            // Initialize step segment timing per step and load number of steps to execute.
            config->_stepping->setTimerPeriod(st.exec_segment->isrPeriod);
            st.step_count = st.exec_segment->n_step;  // NOTE: Can sometimes be zero when moving slow.
            // If the new segment starts a new planner block, initialize stepper variables and counters.
            // NOTE: When the segment data index changes, this indicates a new planner block.
            if (st.exec_block_index != st.exec_segment->st_block_index) {
                st.exec_block_index = st.exec_segment->st_block_index;
                st.exec_block       = &st_block_buffer[st.exec_block_index];
#ifdef DEBUG_STEPPING
                bool offstep = false;
#endif
                // Initialize Bresenham line and distance counters
                for (int axis = 0; axis < n_axis; axis++) {
#ifdef DEBUG_STEPPING
                    if (st.exec_block->entry[axis] != motor_steps[axis]) {
                        offstep = true;
                    }
#endif
                    st.counter[axis] = st.exec_block->step_event_count >> 1;
                }
#ifdef DEBUG_STEPPING
                if (offstep) {
                    for (int axis = 0; axis < n_axis; axis++) {
                        expected_steps[axis] = st.exec_block->entry[axis];
                    }
                    rtCrash = true;
                }
#endif
            }

            st.dir_outbits = st.exec_block->direction_bits;
            // Adjust Bresenham axis increment counters according to AMASS level.
            for (int axis = 0; axis < n_axis; axis++) {
                st.steps[axis] = st.exec_block->steps[axis] >> st.exec_segment->amass_level;
            }
            // Set real-time spindle output as segment is loaded, just prior to the first step.
            spindle->setSpeedfromISR(st.exec_segment->spindle_dev_speed);
        } else {
            // Segment buffer empty. Shutdown.
            stop_stepping();
            if (sys.state != State::Jog) {  // added to prevent ... jog after probing crash
                // Ensure pwm is set properly upon completion of rate-controlled motion.
                if (st.exec_block != NULL && st.exec_block->is_pwm_rate_adjusted) {
                    spindle->setSpeedfromISR(0);
                }
            }
            rtCycleStop = true;
            return;  // Nothing to do but exit.
        }
    }

    // Check probing state.
    if (probeState == ProbeState::Active && config->_probe->tripped()) {
        probeState = ProbeState::Off;
        // Memcpy is not IRAM_ATTR, but: most compilers optimize memcpy away.
        memcpy(probe_steps, motor_steps, sizeof(motor_steps));
        rtMotionCancel = true;
    }

    // Reset step out bits.
    st.step_outbits = 0;

    for (int axis = 0; axis < n_axis; axis++) {
        // Execute step displacement profile by Bresenham line algorithm
        st.counter[axis] += st.steps[axis];
        if (st.counter[axis] > st.exec_block->step_event_count) {
            set_bitnum(st.step_outbits, axis);
            st.counter[axis] -= st.exec_block->step_event_count;
            if (bitnum_is_true(st.exec_block->direction_bits, axis)) {
                motor_steps[axis]--;
            } else {
                motor_steps[axis]++;
            }
        }
    }

    st.step_count--;  // Decrement step events count
    if (st.step_count == 0) {
        // Segment is complete. Discard current segment and advance segment indexing.
        st.exec_segment     = NULL;
        segment_buffer_tail = segment_buffer_tail >= (SEGMENT_BUFFER_SIZE - 1) ? 0 : segment_buffer_tail + 1;
    }

    config->_axes->unstep();
}

// enabled. Startup init and limits call this function but shouldn't start the cycle.
void Stepper::wake_up() {
    //log_info("st_wake_up");
    // Enable stepper drivers.
    config->_axes->set_disable(false);

    // Enable Stepping Driver Interrupt
    config->_stepping->startTimer();
}

void Stepper::go_idle() {
    stop_stepping();
    protocol_disable_steppers();
}

// Reset and clear stepper subsystem variables
void Stepper::reset() {
    // Initialize Stepping driver idle state.
    config->_stepping->reset();

    go_idle();

    // Initialize stepper algorithm variables.
    memset(&prep, 0, sizeof(st_prep_t));
    memset(&st, 0, sizeof(stepper_t));
    st.exec_segment     = NULL;
    pl_block            = NULL;  // Planner block pointer used by segment buffer
    segment_buffer_tail = 0;
    segment_buffer_head = 0;  // empty = tail
    segment_next_head   = 1;
    st.step_outbits     = 0;
    st.dir_outbits      = 0;  // Initialize direction bits to default.
    // TODO do we need to turn step pins off?
}

// Called by planner_recalculate() when the executing block is updated by the new plan.
void Stepper::update_plan_block_parameters() {
    if (pl_block != NULL) {  // Ignore if at start of a new block.
        prep.recalculate_flag.recalculate = 1;
        pl_block->entry_speed_sqr         = prep.current_speed * prep.current_speed;  // Update entry speed.
        pl_block                          = NULL;  // Flag prep_segment() to load and check active velocity profile.
    }
}

// Changes the run state of the step segment buffer to execute the special parking motion.
void Stepper::parking_setup_buffer() {
    // Store step execution data of partially completed block, if necessary.
    if (prep.recalculate_flag.holdPartialBlock) {
        prep.last_st_block_index  = prep.st_block_index;
        prep.last_steps_remaining = prep.steps_remaining;
        prep.last_dt_remainder    = prep.dt_remainder;
        prep.last_step_per_mm     = prep.step_per_mm;
    }
    // Set flags to execute a parking motion
    prep.recalculate_flag.parking     = 1;
    prep.recalculate_flag.recalculate = 0;
    pl_block                          = NULL;  // Always reset parking motion to reload new block.
}

// Restores the step segment buffer to the normal run state after a parking motion.
void Stepper::parking_restore_buffer() {
    // Restore step execution data and flags of partially completed block, if necessary.
    if (prep.recalculate_flag.holdPartialBlock) {
        st_prep_block                          = &st_block_buffer[prep.last_st_block_index];
        prep.st_block_index                    = prep.last_st_block_index;
        prep.steps_remaining                   = prep.last_steps_remaining;
        prep.dt_remainder                      = prep.last_dt_remainder;
        prep.step_per_mm                       = prep.last_step_per_mm;
        prep.recalculate_flag.holdPartialBlock = 1;
        prep.recalculate_flag.recalculate      = 1;
        prep.req_mm_increment                  = REQ_MM_INCREMENT_SCALAR / prep.step_per_mm;  // Recompute this value.
    } else {
        prep.recalculate_flag = {};
    }

    pl_block = NULL;  // Set to reload next block.
}

// Increments the step segment buffer block data ring buffer.
static uint8_t next_block_index(uint8_t block_index) {
    block_index++;
    return block_index == (SEGMENT_BUFFER_SIZE - 1) ? 0 : block_index;
}

/* Prepares step segment buffer. Continuously called from main program.

   The segment buffer is an intermediary buffer interface between the execution of steps
   by the stepper algorithm and the velocity profiles generated by the planner. The stepper
   algorithm only executes steps within the segment buffer and is filled by the main program
   when steps are "checked-out" from the first block in the planner buffer. This keeps the
   step execution and planning optimization processes atomic and protected from each other.
   The number of steps "checked-out" from the planner buffer and the number of segments in
   the segment buffer is sized and computed such that no operation in the main program takes
   longer than the time it takes the stepper algorithm to empty it before refilling it.
   Currently, the segment buffer conservatively holds roughly up to 40-50 msec of steps.
   NOTE: Computation units are in steps, millimeters, and minutes.
*/
void Stepper::prep_buffer() {
    // Block step prep buffer, while in a suspend state and there is no suspend motion to execute.
    if (sys.step_control.endMotion) {
        return;
    }

    while (segment_buffer_tail != segment_next_head) {  // Check if we need to fill the buffer.
        // Determine if we need to load a new planner block or if the block needs to be recomputed.
        if (pl_block == NULL) {
            // Query planner for a queued block
            if (sys.step_control.executeSysMotion) {
                pl_block = plan_get_system_motion_block();
            } else {
                pl_block = plan_get_current_block();
            }

            if (pl_block == NULL) {
                return;  // No planner blocks. Exit.
            }

            // Check if we need to only recompute the velocity profile or load a new block.
            if (prep.recalculate_flag.recalculate) {
                if (prep.recalculate_flag.parking) {
                    prep.recalculate_flag.recalculate = 0;
                } else {
                    prep.recalculate_flag = {};
                }
            } else {
                // Load the Bresenham stepping data for the block.
                prep.st_block_index = next_block_index(prep.st_block_index);
                // Prepare and copy Bresenham algorithm segment data from the new planner block, so that
                // when the segment buffer completes the planner block, it may be discarded when the
                // segment buffer finishes the prepped block, but the stepper ISR is still executing it.
                st_prep_block                 = &st_block_buffer[prep.st_block_index];
                st_prep_block->direction_bits = pl_block->direction_bits;
                uint8_t idx;
                auto    n_axis = config->_axes->_numberAxis;

                // Bit-shift multiply all Bresenham data by the max AMASS level so that
                // we never divide beyond the original data anywhere in the algorithm.
                // If the original data is divided, we can lose a step from integer roundoff.
                for (idx = 0; idx < n_axis; idx++) {
                    st_prep_block->steps[idx] = pl_block->steps[idx] << maxAmassLevel;
#ifdef DEBUG_STEPPING
                    st_prep_block->entry[idx] = pl_block->entry_pos[idx];
                    // st_prep_block->exit[idx]  = pl_block->exit_pos[idx];
#endif
                }
#ifdef DEBUG_STEPPING
                if (pl_block->seq != st_seq && !rtSeq) {
                    rtSeq   = true;
                    st_seq0 = st_seq;
                    pl_seq0 = pl_block->seq;
                }
                ++st_seq;
#endif
                st_prep_block->step_event_count = pl_block->step_event_count << maxAmassLevel;

                // Initialize segment buffer data for generating the segments.
                prep.steps_remaining  = (float)pl_block->step_event_count;
                prep.step_per_mm      = prep.steps_remaining / pl_block->millimeters;
                prep.req_mm_increment = REQ_MM_INCREMENT_SCALAR / prep.step_per_mm;
                prep.dt_remainder     = 0.0;  // Reset for new segment block
                if ((sys.step_control.executeHold) || prep.recalculate_flag.decelOverride) {
                    // New block loaded mid-hold. Override planner block entry speed to enforce deceleration.
                    prep.current_speed                  = prep.exit_speed;
                    pl_block->entry_speed_sqr           = prep.exit_speed * prep.exit_speed;
                    prep.recalculate_flag.decelOverride = 0;
                } else {
                    prep.current_speed = float(sqrt(pl_block->entry_speed_sqr));
                }

                // prep.inv_rate is only used if is_pwm_rate_adjusted is true
                st_prep_block->is_pwm_rate_adjusted = false;  // set default value

                if (spindle->isRateAdjusted()) {
                    if (pl_block->spindle == SpindleState::Ccw) {
                        // Pre-compute inverse programmed rate to speed up PWM updating per step segment.
                        prep.inv_rate                       = 1.0f / pl_block->programmed_rate;
                        st_prep_block->is_pwm_rate_adjusted = true;
                    }
                }
            }
            /* ---------------------------------------------------------------------------------
             Compute the velocity profile of a new planner block based on its entry and exit
             speeds, or recompute the profile of a partially-completed planner block if the
             planner has updated it. For a commanded forced-deceleration, such as from a feed
             hold, override the planner velocities and decelerate to the target exit speed.
            */
            prep.mm_complete  = 0.0;  // Default velocity profile complete at 0.0mm from end of block.
            float inv_2_accel = 0.5f / pl_block->acceleration;
            if (sys.step_control.executeHold) {  // [Forced Deceleration to Zero Velocity]
                // Compute velocity profile parameters for a feed hold in-progress. This profile overrides
                // the planner block profile, enforcing a deceleration to zero speed.
                prep.ramp_type = RAMP_DECEL;
                // Compute decelerate distance relative to end of block.
                float decel_dist = pl_block->millimeters - inv_2_accel * pl_block->entry_speed_sqr;
                if (decel_dist < 0.0) {
                    // Deceleration through entire planner block. End of feed hold is not in this block.
                    prep.exit_speed = float(sqrt(pl_block->entry_speed_sqr - 2 * pl_block->acceleration * pl_block->millimeters));
                } else {
                    prep.mm_complete = decel_dist;  // End of feed hold.
                    prep.exit_speed  = 0.0;
                }
            } else {  // [Normal Operation]
                // Compute or recompute velocity profile parameters of the prepped planner block.
                prep.ramp_type        = RAMP_ACCEL;  // Initialize as acceleration ramp.
                prep.accelerate_until = pl_block->millimeters;
                float exit_speed_sqr;
                float nominal_speed;
                if (sys.step_control.executeSysMotion) {
                    prep.exit_speed = exit_speed_sqr = 0.0;  // Enforce stop at end of system motion.
                } else {
                    exit_speed_sqr  = plan_get_exec_block_exit_speed_sqr();
                    prep.exit_speed = float(sqrt(exit_speed_sqr));
                }

                nominal_speed            = plan_compute_profile_nominal_speed(pl_block);
                float nominal_speed_sqr  = nominal_speed * nominal_speed;
                float intersect_distance = 0.5f * (pl_block->millimeters + inv_2_accel * (pl_block->entry_speed_sqr - exit_speed_sqr));
                if (pl_block->entry_speed_sqr > nominal_speed_sqr) {  // Only occurs during override reductions.
                    prep.accelerate_until = pl_block->millimeters - inv_2_accel * (pl_block->entry_speed_sqr - nominal_speed_sqr);
                    if (prep.accelerate_until <= 0.0) {  // Deceleration-only.
                        prep.ramp_type = RAMP_DECEL;
                        // prep.decelerate_after = pl_block->millimeters;
                        // prep.maximum_speed = prep.current_speed;
                        // Compute override block exit speed since it doesn't match the planner exit speed.
                        prep.exit_speed = float(sqrt(pl_block->entry_speed_sqr - 2 * pl_block->acceleration * pl_block->millimeters));
                        prep.recalculate_flag.decelOverride = 1;  // Flag to load next block as deceleration override.
                        // TODO: Determine correct handling of parameters in deceleration-only.
                        // Can be tricky since entry speed will be current speed, as in feed holds.
                        // Also, look into near-zero speed handling issues with this.
                    } else {
                        // Decelerate to cruise or cruise-decelerate types. Guaranteed to intersect updated plan.
                        prep.decelerate_after = inv_2_accel * (nominal_speed_sqr - exit_speed_sqr);
                        prep.maximum_speed    = nominal_speed;
                        prep.ramp_type        = RAMP_DECEL_OVERRIDE;
                    }
                } else if (intersect_distance > 0.0) {
                    if (intersect_distance < pl_block->millimeters) {  // Either trapezoid or triangle types
                        // NOTE: For acceleration-cruise and cruise-only types, following calculation will be 0.0.
                        prep.decelerate_after = inv_2_accel * (nominal_speed_sqr - exit_speed_sqr);
                        if (prep.decelerate_after < intersect_distance) {  // Trapezoid type
                            prep.maximum_speed = nominal_speed;
                            if (pl_block->entry_speed_sqr == nominal_speed_sqr) {
                                // Cruise-deceleration or cruise-only type.
                                prep.ramp_type = RAMP_CRUISE;
                            } else {
                                // Full-trapezoid or acceleration-cruise types
                                prep.accelerate_until -= inv_2_accel * (nominal_speed_sqr - pl_block->entry_speed_sqr);
                            }
                        } else {  // Triangle type
                            prep.accelerate_until = intersect_distance;
                            prep.decelerate_after = intersect_distance;
                            prep.maximum_speed    = float(sqrt(2.0f * pl_block->acceleration * intersect_distance + exit_speed_sqr));
                        }
                    } else {  // Deceleration-only type
                        prep.ramp_type = RAMP_DECEL;
                        // prep.decelerate_after = pl_block->millimeters;
                        // prep.maximum_speed = prep.current_speed;
                    }
                } else {  // Acceleration-only type
                    prep.accelerate_until = 0.0;
                    // prep.decelerate_after = 0.0;
                    prep.maximum_speed = prep.exit_speed;
                }
            }

            sys.step_control.updateSpindleSpeed = true;  // Force update whenever updating block.
        }

        // Initialize new segment
        volatile segment_t* prep_segment = &segment_buffer[segment_buffer_head];

#ifdef DEBUG_STEPPING
        prep_segment->seq = seg_seq0++;
#endif

        // Set new segment to point to the current segment data block.
        prep_segment->st_block_index = prep.st_block_index;

        /*------------------------------------------------------------------------------------
            Compute the average velocity of this new segment by determining the total distance
          traveled over the segment time DT_SEGMENT. The following code first attempts to create
          a full segment based on the current ramp conditions. If the segment time is incomplete
          when terminating at a ramp state change, the code will continue to loop through the
          progressing ramp states to fill the remaining segment execution time. However, if
          an incomplete segment terminates at the end of the velocity profile, the segment is
          considered completed despite having a truncated execution time less than DT_SEGMENT.
            The velocity profile is always assumed to progress through the ramp sequence:
          acceleration ramp, cruising state, and deceleration ramp. Each ramp's travel distance
          may range from zero to the length of the block. Velocity profiles can end either at
          the end of planner block (typical) or mid-block at the end of a forced deceleration,
          such as from a feed hold.
        */
        float dt_max   = DT_SEGMENT;                                // Maximum segment time
        float dt       = 0.0;                                       // Initialize segment time
        float time_var = dt_max;                                    // Time worker variable
        float mm_var;                                               // mm-Distance worker variable
        float speed_var;                                            // Speed worker variable
        float mm_remaining = pl_block->millimeters;                 // New segment distance from end of block.
        float minimum_mm   = mm_remaining - prep.req_mm_increment;  // Guarantee at least one step.

        if (minimum_mm < 0.0) {
            minimum_mm = 0.0;
        }

        do {
            switch (prep.ramp_type) {
                case RAMP_DECEL_OVERRIDE:
                    speed_var = pl_block->acceleration * time_var;
                    mm_var    = time_var * (prep.current_speed - 0.5f * speed_var);
                    mm_remaining -= mm_var;
                    if ((mm_remaining < prep.accelerate_until) || (mm_var <= 0)) {
                        // Cruise or cruise-deceleration types only for deceleration override.
                        mm_remaining       = prep.accelerate_until;  // NOTE: 0.0 at EOB
                        time_var           = 2.0f * (pl_block->millimeters - mm_remaining) / (prep.current_speed + prep.maximum_speed);
                        prep.ramp_type     = RAMP_CRUISE;
                        prep.current_speed = prep.maximum_speed;
                    } else {  // Mid-deceleration override ramp.
                        prep.current_speed -= speed_var;
                    }
                    break;
                case RAMP_ACCEL:
                    // NOTE: Acceleration ramp only computes during first do-while loop.
                    speed_var = pl_block->acceleration * time_var;
                    mm_remaining -= time_var * (prep.current_speed + 0.5f * speed_var);
                    if (mm_remaining < prep.accelerate_until) {  // End of acceleration ramp.
                        // Acceleration-cruise, acceleration-deceleration ramp junction, or end of block.
                        mm_remaining = prep.accelerate_until;  // NOTE: 0.0 at EOB
                        time_var     = 2.0f * (pl_block->millimeters - mm_remaining) / (prep.current_speed + prep.maximum_speed);
                        if (mm_remaining == prep.decelerate_after) {
                            prep.ramp_type = RAMP_DECEL;
                        } else {
                            prep.ramp_type = RAMP_CRUISE;
                        }
                        prep.current_speed = prep.maximum_speed;
                    } else {  // Acceleration only.
                        prep.current_speed += speed_var;
                    }
                    break;
                case RAMP_CRUISE:
                    // NOTE: mm_var used to retain the last mm_remaining for incomplete segment time_var calculations.
                    // NOTE: If maximum_speed*time_var value is too low, round-off can cause mm_var to not change. To
                    //   prevent this, simply enforce a minimum speed threshold in the planner.
                    mm_var = mm_remaining - prep.maximum_speed * time_var;
                    if (mm_var < prep.decelerate_after) {  // End of cruise.
                        // Cruise-deceleration junction or end of block.
                        time_var       = (mm_remaining - prep.decelerate_after) / prep.maximum_speed;
                        mm_remaining   = prep.decelerate_after;  // NOTE: 0.0 at EOB
                        prep.ramp_type = RAMP_DECEL;
                    } else {  // Cruising only.
                        mm_remaining = mm_var;
                    }
                    break;
                default:  // case RAMP_DECEL:
                    // NOTE: mm_var used as a misc worker variable to prevent errors when near zero speed.
                    speed_var = pl_block->acceleration * time_var;  // Used as delta speed (mm/min)
                    if (prep.current_speed > speed_var) {           // Check if at or below zero speed.
                        // Compute distance from end of segment to end of block.
                        mm_var = mm_remaining - time_var * (prep.current_speed - 0.5f * speed_var);  // (mm)
                        if (mm_var > prep.mm_complete) {                                             // Typical case. In deceleration ramp.
                            mm_remaining = mm_var;
                            prep.current_speed -= speed_var;
                            break;  // Segment complete. Exit switch-case statement. Continue do-while loop.
                        }
                    }
                    // Otherwise, at end of block or end of forced-deceleration.
                    time_var           = 2.0f * (mm_remaining - prep.mm_complete) / (prep.current_speed + prep.exit_speed);
                    mm_remaining       = prep.mm_complete;
                    prep.current_speed = prep.exit_speed;
            }

            dt += time_var;  // Add computed ramp time to total segment time.
            if (dt < dt_max) {
                time_var = dt_max - dt;  // **Incomplete** At ramp junction.
            } else {
                if (mm_remaining > minimum_mm) {  // Check for very slow segments with zero steps.
                    // Increase segment time to ensure at least one step in segment. Override and loop
                    // through distance calculations until minimum_mm or mm_complete.
                    dt_max += DT_SEGMENT;
                    time_var = dt_max - dt;
                } else {
                    break;  // **Complete** Exit loop. Segment execution time maxed.
                }
            }
        } while (mm_remaining > prep.mm_complete);  // **Complete** Exit loop. Profile complete.

        /* -----------------------------------------------------------------------------------
          Compute spindle speed PWM output for step segment
        */
        if (st_prep_block->is_pwm_rate_adjusted || sys.step_control.updateSpindleSpeed) {
            if (pl_block->spindle != SpindleState::Disable) {
                float speed = pl_block->spindle_speed;
                // NOTE: Feed and rapid overrides are independent of PWM value and do not alter laser power/rate.
                if (st_prep_block->is_pwm_rate_adjusted) {
                    speed *= (prep.current_speed * prep.inv_rate);
                    // log_debug("RPM " << rpm);
                    // log_debug("Rates CV " << prep.current_speed << " IV " << prep.inv_rate << " RPM " << rpm);
                }
                // If current_speed is zero, then may need to be rpm_min*(100/MAX_SPINDLE_SPEED_OVERRIDE)
                // but this would be instantaneous only and during a motion. May not matter at all.

                prep.current_spindle_speed = speed;
            } else {
                sys.spindle_speed          = 0;
                prep.current_spindle_speed = 0;
            }
            sys.step_control.updateSpindleSpeed = false;
        }
        prep_segment->spindle_speed     = prep.current_spindle_speed;
        prep_segment->spindle_dev_speed = spindle->mapSpeed(prep.current_spindle_speed);  // Reload segment PWM value

        /* -----------------------------------------------------------------------------------
           Compute segment step rate, steps to execute, and apply necessary rate corrections.
           NOTE: Steps are computed by direct scalar conversion of the millimeter distance
           remaining in the block, rather than incrementally tallying the steps executed per
           segment. This helps in removing floating point round-off issues of several additions.
           However, since floats have only 7.2 significant digits, long moves with extremely
           high step counts can exceed the precision of floats, which can lead to lost steps.
           Fortunately, this scenario is highly unlikely and unrealistic in typical DIY CNC
           machines (i.e. exceeding 10 meters axis travel at 200 step/mm).
        */
        float step_dist_remaining    = prep.step_per_mm * mm_remaining;                       // Convert mm_remaining to steps
        float n_steps_remaining      = float(ceil(step_dist_remaining));                      // Round-up current steps remaining
        float last_n_steps_remaining = float(ceil(prep.steps_remaining));                     // Round-up last steps remaining
        prep_segment->n_step         = uint16_t(last_n_steps_remaining - n_steps_remaining);  // Compute number of steps to execute.

        // Bail if we are at the end of a feed hold and don't have a step to execute.
        if (prep_segment->n_step == 0) {
            if (sys.step_control.executeHold) {
                // Less than one step to decelerate to zero speed, but already very close. AMASS
                // requires full steps to execute. So, just bail.
                sys.step_control.endMotion = true;
                if (!(prep.recalculate_flag.parking)) {
                    prep.recalculate_flag.holdPartialBlock = 1;
                }
                return;  // Segment not generated, but current step data still retained.
            }
        }

        // Compute segment step rate. Since steps are integers and mm distances traveled are not,
        // the end of every segment can have a partial step of varying magnitudes that are not
        // executed, because the stepper ISR requires whole steps due to the AMASS algorithm. To
        // compensate, we track the time to execute the previous segment's partial step and simply
        // apply it with the partial step distance to the current segment, so that it minutely
        // adjusts the whole segment rate to keep step output exact. These rate adjustments are
        // typically very small and do not adversely effect performance, but ensures that the
        // system outputs the exact acceleration and velocity profiles computed by the planner.

        dt += prep.dt_remainder;  // Apply previous segment partial step execute time
        // dt is in minutes so inv_rate is in minutes
        float inv_rate = dt / (last_n_steps_remaining - step_dist_remaining);  // Compute adjusted step rate inverse

        // Compute CPU cycles per step for the prepped segment.
        // fStepperTimer is in units of timerTicks/sec, so the dimensional analysis is
        // timerTicks/sec * 60 sec/minute * minutes = timerTicks
        uint32_t timerTicks = uint32_t(ceil((Machine::Stepping::fStepperTimer * 60) * inv_rate));  // (timerTicks/step)
        int      level;

        // Compute step timing and multi-axis smoothing level.
        for (level = 0; level < maxAmassLevel; level++) {
            if (timerTicks < amassThreshold) {
                break;
            }
            timerTicks >>= 1;
        }
        prep_segment->amass_level = level;
        prep_segment->n_step <<= level;
        // isrPeriod is stored as 16 bits, so limit timerTicks to the
        // largest value that will fit in a uint16_t.
        prep_segment->isrPeriod = timerTicks > 0xffff ? 0xffff : timerTicks;

        // Segment complete! Increment segment buffer indices, so stepper ISR can immediately execute it.
        auto lastseg        = segment_next_head;
        segment_next_head   = segment_next_head >= (SEGMENT_BUFFER_SIZE - 1) ? 0 : segment_next_head + 1;
        segment_buffer_head = lastseg;

        // Update the appropriate planner and segment data.
        pl_block->millimeters = mm_remaining;
        prep.steps_remaining  = n_steps_remaining;
        prep.dt_remainder     = (n_steps_remaining - step_dist_remaining) * inv_rate;
        // Check for exit conditions and flag to load next planner block.
        if (mm_remaining == prep.mm_complete) {
            // End of planner block or forced-termination. No more distance to be executed.
            if (mm_remaining > 0.0) {  // At end of forced-termination.
                // Reset prep parameters for resuming and then bail. Allow the stepper ISR to complete
                // the segment queue, where realtime protocol will set new state upon receiving the
                // cycle stop flag from the ISR. Prep_segment is blocked until then.
                sys.step_control.endMotion = true;
                if (!(prep.recalculate_flag.parking)) {
                    prep.recalculate_flag.holdPartialBlock = 1;
                }
                return;  // Bail!
            } else {     // End of planner block
                // The planner block is complete. All steps are set to be executed in the segment buffer.
                if (sys.step_control.executeSysMotion) {
                    sys.step_control.endMotion = true;
                    return;
                }
                pl_block = NULL;  // Set pointer to indicate check and load next planner block.
                plan_discard_current_block();
            }
        }
    }
}

// Called by realtime status reporting to fetch the current speed being executed. This value
// however is not exactly the current speed, but the speed computed in the last step segment
// in the segment buffer. It will always be behind by up to the number of segment blocks (-1)
// divided by the ACCELERATION TICKS PER SECOND in seconds.
float Stepper::get_realtime_rate() {
    switch (sys.state) {
        case State::Cycle:
        case State::Homing:
        case State::Hold:
        case State::Jog:
        case State::SafetyDoor:
            return prep.current_speed;
        default:
            return 0.0f;
    }
}
