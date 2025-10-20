// #include "Driver/i2s_out.h"
#include "EnumItem.h"
#include "Stepping.h"
#include "Machine/MachineConfig.h"  // config

#include <atomic>

step_engine_t* step_engines = NULL;  // Linked list of stepping engines

step_engine_t* find_engine(const char* name) {
    for (step_engine_t* p = step_engines; p; p = p->link) {
        // Initial substring match, handles different forms of I2S
        if (strncmp(name, p->name, strlen(p->name)) == 0) {
            return p;
        }
    }
    return NULL;
}

namespace Machine {

    // fStepperTimer should be an integer divisor of the bus speed, i.e. of fTimers
    const int ticksPerMicrosecond = Stepping::fStepperTimer / 1000000;

    uint32_t Stepping::_engine = DEFAULT_STEPPING_ENGINE;

    AxisMask Stepping::direction_mask = 0;

    bool    Stepping::_switchedStepper = false;
    int32_t Stepping::_segments        = 12;

    uint32_t Stepping::_idleMsecs           = 255;
    uint32_t Stepping::_pulseUsecs          = 4;
    uint32_t Stepping::_directionDelayUsecs = 0;
    uint32_t Stepping::_disableDelayUsecs   = 0;

    step_engine_t* Stepping::step_engine;

    const EnumItem stepTypes[] = { { Stepping::TIMED, "Timed" },
#if MAX_N_RMT
                                   { Stepping::RMT_ENGINE, "RMT" },
#endif
#if MAX_N_I2SO
                                   { Stepping::I2S_STATIC, "I2S_STATIC" },
                                   { Stepping::I2S_STREAM, "I2S_STREAM" },
#endif
                                   EnumItem(DEFAULT_STEPPING_ENGINE) };

    void Stepping::afterParse() {
        const char* name = stepTypes[_engine].name;
        step_engine      = find_engine(name);
        Assert(step_engine, "Cannot find stepping engine for %s", name);
#if MAX_N_I2SO
        Assert(strcmp("I2S", name) || config->_i2so, "I2SO bus must be configured for this stepping type");
#endif
    }

    void Stepping::init() {
        log_info("Stepping:" << stepTypes[_engine].name << " Pulse:" << _pulseUsecs << "us Dsbl Delay:" << _disableDelayUsecs
                             << "us Dir Delay:" << _directionDelayUsecs << "us Idle Delay:" << _idleMsecs << "ms");

        uint32_t actual = step_engine->init(_directionDelayUsecs, _pulseUsecs, fStepperTimer, Stepper::pulse_func);
        if (actual != _pulseUsecs) {
            log_warn("stepping/pulse_us adjusted to " << actual);
        }

        // Register pulse_func with the I2S subsystem
        // This could be done via the linker.
        //        i2s_out_set_pulse_callback(Stepper::pulse_func);

        Stepper::init();
    }

}

Stepping::motor_pins_t* Stepping::axis_motors[MAX_N_AXIS][MAX_MOTORS_PER_AXIS] = { nullptr };

void Stepping::assignMotor(axis_t axis, motor_t motor, pinnum_t step_pin, bool step_invert, pinnum_t dir_pin, bool dir_invert) {
    step_pin = step_engine->init_step_pin(step_pin, step_invert);

    auto m                   = new motor_pins_t;
    axis_motors[axis][motor] = m;
    m->step_pin              = step_pin;
    m->step_invert           = step_invert;
    m->dir_pin               = dir_pin;
    m->dir_invert            = dir_invert;
    m->blocked               = false;
    m->limited               = false;

    if (motor == 0 && dir_invert) {
        set_bitnum(direction_mask, axis);
    }
}

steps_t Stepping::axis_steps[MAX_N_AXIS] = { 0 };

bool* Stepping::limit_var(axis_t axis, motor_t motor) {
    auto m = axis_motors[axis][motor];
    return m ? &(m->limited) : nullptr;
}

void Stepping::block(axis_t axis, motor_t motor) {
    auto m = axis_motors[axis][motor];
    if (m) {
        m->blocked = true;
    }
}

void Stepping::unblock(axis_t axis, motor_t motor) {
    auto m = axis_motors[axis][motor];
    if (m) {
        m->blocked = false;
    }
}

void Stepping::limit(axis_t axis, motor_t motor) {
    auto m = axis_motors[axis][motor];
    if (m) {
        m->limited = true;
    }
}
void Stepping::unlimit(axis_t axis, motor_t motor) {
    auto m = axis_motors[axis][motor];
    if (m) {
        m->limited = false;
    }
}

void IRAM_ATTR Stepping::step(AxisMask step_mask, AxisMask dir_mask) {
    // Set the direction pins, but optimize for the common
    // situation where the direction bits haven't changed.
    static AxisMask previous_dir_mask = 65535;  // should never be this value
    if (previous_dir_mask == 65535) {
        // Set all the direction bits the first time
        previous_dir_mask = ~dir_mask;
    }

    if (dir_mask != previous_dir_mask) {
        for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
            bool dir     = bitnum_is_true(dir_mask, axis);
            bool old_dir = bitnum_is_true(previous_dir_mask, axis);
            if (dir != old_dir) {
                for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                    auto m = axis_motors[axis][motor];
                    if (m) {
                        step_engine->set_dir_pin(m->dir_pin, dir ^ m->dir_invert);
                    }
                }
            }
            // Some stepper drivers need time between changing direction and doing a pulse.
            step_engine->finish_dir();
        }
        previous_dir_mask = dir_mask;
    }

    step_engine->start_step();

    // Turn on step pulses for motors that are supposed to step now
    for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
        if (bitnum_is_true(step_mask, axis)) {
            auto increment = bitnum_is_true(dir_mask, axis) ? -1 : 1;
            axis_steps[axis] += increment;
            for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                auto m = axis_motors[axis][motor];
                if (m && !m->blocked && !m->limited) {
                    step_engine->set_step_pin(m->step_pin, !m->step_invert);
                }
            }
        }
    }
    step_engine->finish_step();
}

// Turn all stepper pins off
void IRAM_ATTR Stepping::unstep() {
    if (step_engine->start_unstep()) {
        return;
    }
    for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
        for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
            auto m = axis_motors[axis][motor];
            if (m) {
                step_engine->set_step_pin(m->step_pin, m->step_invert);
            }
        }
    }
    step_engine->finish_unstep();
}

void Stepping::reset() {}
void Stepping::beginLowLatency() {}
void Stepping::endLowLatency() {}

// Called only from Stepper::pulse_func when a new segment is loaded
// The argument is in units of ticks of the timer that generates ISRs
void IRAM_ATTR Stepping::setTimerPeriod(uint32_t ticks) {
    step_engine->set_timer_ticks((uint32_t)ticks);
}

// Called only from Stepper::wake_up which is not used in ISR context
void Stepping::startTimer() {
    step_engine->start_timer();
}

// Called only from Stepper::stop_stepping, used in both ISR and foreground contexts
void IRAM_ATTR Stepping::stopTimer() {
    step_engine->stop_timer();
}

void Stepping::group(Configuration::HandlerBase& handler) {
    handler.item("engine", _engine, stepTypes);
    handler.item("idle_ms", _idleMsecs, 0, 10000000);  // full range
    handler.item("pulse_us", _pulseUsecs, 0, 30);
    handler.item("dir_delay_us", _directionDelayUsecs, 0, 10);
    handler.item("disable_delay_us", _disableDelayUsecs, 0, 1000000);  // max 1 second
    handler.item("segments", _segments, 6, 20);
}

uint32_t Stepping::maxPulsesPerSec() {
    return step_engine->max_pulses_per_sec();
}
