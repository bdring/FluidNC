// #include "Driver/i2s_out.h"
#include "EnumItem.h"
#include "Stepping.h"
#include "Machine/MachineConfig.h"  // config
#include "Motors/MotorDriver.h"     // MotorDrivers::MotorDriver

#include <atomic>

std::vector<step_engine_t*> step_engines;

namespace Machine {

    step_engine_t* Stepping::_engine = nullptr;

    AxisMask Stepping::direction_mask = 0;

    bool    Stepping::_switchedStepper = false;
    int32_t Stepping::_segments        = 12;

    uint32_t Stepping::fStepperTimer        = 0;
    uint32_t Stepping::_idleMsecs           = 255;
    uint32_t Stepping::_pulseUsecs          = 4;
    uint32_t Stepping::_directionDelayUsecs = 0;
    uint32_t Stepping::_disableDelayUsecs   = 0;

    const EnumItem stepTypes[] = { { Stepping::TIMED, "Timed" },
#if MAX_N_RMT
                                   { Stepping::RMT_ENGINE, "RMT" },
#endif
#if MAX_N_I2SO
                                   { Stepping::I2S_STATIC, "I2S_STATIC" }, { Stepping::I2S_STREAM, "I2S_STREAM" },
#endif
#if MAX_N_SIMULATOR
                                   { Stepping::SIMULATOR, "Simulator" },
#endif
#if defined(MAX_N_PIO) && MAX_N_PIO
                                   { Stepping::PIO_ENGINE, "PIO" },
#endif
                                   EnumItem(DEFAULT_STEPPING_ENGINE) };

    void Stepping::afterParse() {
        if (!_engine) {
            _engine = step_engines[0];
        }
#if MAX_N_I2SO
        Assert(strncmp("I2S", _engine->name, 3) || config->_i2so, "I2SO bus must be configured for this stepping type");
#endif
    }

    void Stepping::init() {
        log_info("Stepping:" << _engine->name << " Pulse:" << _pulseUsecs << "us Dsbl Delay:" << _disableDelayUsecs
                             << "us Dir Delay:" << _directionDelayUsecs << "us Idle Delay:" << _idleMsecs << "ms");

        uint32_t actual = _engine->init(_directionDelayUsecs, _pulseUsecs, fStepperTimer, Stepper::pulse_func);
        if (actual != _pulseUsecs) {
            log_warn("stepping/pulse_us adjusted to " << actual);
        }

        Stepper::init();
    }
}

Stepping::motor_pins_t* Stepping::axis_motors[MAX_N_AXIS][MAX_MOTORS_PER_AXIS] = { nullptr };

void Stepping::assignMotor(axis_t axis, motor_t motor, pinnum_t step_pin, bool step_invert, pinnum_t dir_pin, bool dir_invert) {
    step_pin = _engine->init_step_pin(step_pin, step_invert);

    // Value-initialized so that step_fn/dir_fn are null rather than
    // indeterminate.  step() only reads them when driver is non-null, which it
    // is not here, but leaving them unset is a trap for the next reader.
    auto m                   = new motor_pins_t {};
    axis_motors[axis][motor] = m;
    m->step_pin              = step_pin;
    m->step_invert           = step_invert;
    m->dir_pin               = dir_pin;
    m->dir_invert            = dir_invert;
    m->blocked               = false;
    m->limited               = false;
    m->driver                = nullptr;

    if (motor == 0 && dir_invert) {
        set_bitnum(direction_mask, axis);
    }
}

void Stepping::assignMotorDriver(axis_t axis, motor_t motor, MotorDrivers::MotorDriver* driver) {
    // Value-initialize, since the step/dir pin fields are unused for a motor
    // whose driver does its own stepping.
    auto m                   = new motor_pins_t {};
    axis_motors[axis][motor] = m;
    m->driver                = driver;
    // Resolved here, with the flash cache enabled, so that step() need not read
    // the driver's vtable out of flash while running as an interrupt handler.
    m->step_fn = driver->isr_step_fn();
    m->dir_fn  = driver->isr_dir_fn();
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
                        if (m->driver) {
                            if (m->dir_fn) {
                                m->dir_fn(m->driver, dir);
                            }
                        } else {
                            _engine->set_dir_pin(m->dir_pin, dir ^ m->dir_invert);
                        }
                    }
                }
            }
            // Some stepper drivers need time between changing direction and doing a pulse.
            _engine->finish_dir();
        }
        previous_dir_mask = dir_mask;
    }

    _engine->start_step();

    // Turn on step pulses for motors that are supposed to step now
    for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
        if (bitnum_is_true(step_mask, axis)) {
            auto increment = bitnum_is_true(dir_mask, axis) ? -1 : 1;
            axis_steps[axis] += increment;
            for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                auto m = axis_motors[axis][motor];
                if (m && !m->blocked && !m->limited) {
                    if (m->driver) {
                        if (m->step_fn) {
                            m->step_fn(m->driver);
                        }
                    } else {
                        _engine->set_step_pin(m->step_pin, !m->step_invert);
                    }
                }
            }
        }
    }
    _engine->finish_step();
}

// Turn all stepper pins off
void IRAM_ATTR Stepping::unstep() {
    if (_engine->start_unstep()) {
        return;
    }
    for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
        for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
            auto m = axis_motors[axis][motor];
            // Driver-stepped motors hold their outputs between steps, so there
            // is no pulse to end for them.
            if (m && !m->driver) {
                _engine->set_step_pin(m->step_pin, m->step_invert);
            }
        }
    }
    _engine->finish_unstep();
}

void Stepping::reset() {}
void Stepping::beginLowLatency() {}
void Stepping::endLowLatency() {}

// Called only from Stepper::pulse_func when a new segment is loaded
// The argument is in units of ticks of the timer that generates ISRs
void IRAM_ATTR Stepping::setTimerPeriod(uint32_t ticks) {
    _engine->set_timer_ticks((uint32_t)ticks);
}

// Called only from Stepper::wake_up which is not used in ISR context
void Stepping::startTimer() {
    _engine->start_timer();
}

// Called only from Stepper::stop_stepping, used in both ISR and foreground contexts
void IRAM_ATTR Stepping::stopTimer() {
    _engine->stop_timer();
}

void Stepping::group(Configuration::HandlerBase& handler) {
    // @config engine
    // @default (none)
    // @default_note board-dependent (DEFAULT_STEPPING_ENGINE, applied in afterParse())
    // Method used to generate step pulses in firmware. Controller board hardware is
    // designed for either RMT or I2S stepping, so this must match what the board
    // actually wires up -- stepping types cannot be mixed across motors. Choices come
    // from stepTypes[] below.
    //
    // RMT drives native GPIO step/direction pins directly using the ESP32 RMT
    // peripheral, with no CPU delay loops; typically used on boards with few motors.
    // TIMED has the same pin requirements as RMT but drives pins from the CPU with
    // delay loops, so there's no reason to prefer it over RMT.
    // I2S_STATIC and I2S_STREAM both drive motors over the I2S-output ("I2SO") shift
    // register bus instead of native GPIOs, to support more motors with fewer pins;
    // they are functionally identical to each other (two names for historical reasons)
    // and require a valid i2so: section elsewhere in the config.
    handler.item("engine", _engine);

    // @config idle_ms
    // @default 255
    // @default_note special "never auto-disable" value (Grbl compatibility)
    // @tuning typical
    // Milliseconds of inactivity before motors are automatically disabled. Any value
    // other than 255 (0-254 or 256+) is a real delay. Motors can also be disabled
    // manually at any time with $MD.
    handler.item("idle_ms", _idleMsecs, 0, 10000000);  // full range

    // @config pulse_us
    // @default 4
    // @tuning typical
    // Duration, in microseconds, of the "on" part of each step pulse; it typically
    // needs an equal "off" duration, so this caps the max step rate at roughly
    // 1000000/(2*pulse_us + dir_delay_us) steps/sec. Too short a pulse won't be
    // registered by some stepper drivers -- check the driver's datasheet if unsure.
    handler.item("pulse_us", _pulseUsecs, 0, 30);

    // @config dir_delay_us
    // @default 0
    // @tuning typical
    // Delay, in microseconds, required between a direction change and the next step
    // pulse. Most drivers don't need this and can leave it at 0.
    handler.item("dir_delay_us", _directionDelayUsecs, 0, 10);

    // @config disable_delay_us
    // @default 0
    // @tuning typical
    // Delay, in microseconds, some motors need between being enabled and being able
    // to take their first step.
    handler.item("disable_delay_us", _disableDelayUsecs, 0, 1000000);  // max 1 second

    // @config segments
    // @default 12
    // Number of entries in the step-segment buffer sitting between the step-execution
    // algorithm and the planner blocks. Governs how much lead time step execution has
    // for other processing (feedhold/override latency is roughly 10ms * segments);
    // leave at the default unless fine-tuning a specialized application.
    handler.item("segments", _segments, 6, 20);
}

uint32_t Stepping::maxPulsesPerSec() {
    return _engine->max_pulses_per_sec();
}
