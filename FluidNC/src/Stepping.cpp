#include "I2SOut.h"
#include "EnumItem.h"
#include "Stepping.h"
#include "Stepper.h"
#include "Machine/MachineConfig.h"  // config

#include <atomic>

namespace Machine {

    int Stepping::_engine = RMT;

    EnumItem stepTypes[] = { { Stepping::TIMED, "Timed" },
                             { Stepping::RMT, "RMT" },
                             { Stepping::I2S_STATIC, "I2S_static" },
                             { Stepping::I2S_STREAM, "I2S_stream" },
                             EnumItem(Stepping::RMT) };

    void Stepping::init() {
        log_info("Stepping:" << stepTypes[_engine].name << " Pulse:" << _pulseUsecs << "us Dsbl Delay:" << _disableDelayUsecs
                             << "us Dir Delay:" << _directionDelayUsecs << "us Idle Delay:" << _idleMsecs << "ms");

        // Prepare stepping interrupt callbacks.  The one that is actually
        // used is determined by timerStart() and timerStop()

        // Setup a timer for direct stepping
        stepTimerInit(fStepperTimer, Stepper::pulse_func);

        // Register pulse_func with the I2S subsystem
        // This could be done via the linker.
        //        i2s_out_set_pulse_callback(Stepper::pulse_func);

        Stepper::init();
    }

    void Stepping::reset() {
        if (_engine == I2S_STREAM) {
            i2s_out_reset();
        }
    }
    void Stepping::beginLowLatency() {
        _switchedStepper = _engine == I2S_STREAM;
        if (_switchedStepper) {
            _engine = I2S_STATIC;
            i2s_out_set_passthrough();
            i2s_out_delay();  // Wait for a change in mode.
        }
    }
    void Stepping::endLowLatency() {
        if (_switchedStepper) {
            if (i2s_out_get_pulser_status() != PASSTHROUGH) {
                // Called during streaming. Stop streaming.
                // log_debug("Stop the I2S streaming and switch to the passthrough mode.");
                i2s_out_set_passthrough();
                i2s_out_delay();  // Wait for a change in mode.
            }
            _engine = I2S_STREAM;
        }
    }
    // Called only from Axes::unstep()
    void IRAM_ATTR Stepping::waitPulse() {
        if (_engine == I2S_STATIC || _engine == TIMED) {
            spinUntil(_stepPulseEndTime);
        }
    }

    // Called only from Axes::step()
    void IRAM_ATTR Stepping::waitDirection() {
        if (_directionDelayUsecs) {
            // Stepper drivers need some time between changing direction and doing a pulse.
            // Do not use switch() in IRAM
            if (_engine == stepper_id_t::I2S_STREAM) {
                // Commit the pin changes to the DMA queue
                i2s_out_push_sample(_directionDelayUsecs);
            } else if (_engine == stepper_id_t::I2S_STATIC) {
                // Commit the pin changes to the hardware immediately
                i2s_out_push();
                delay_us(_directionDelayUsecs);
            } else if (_engine == stepper_id_t::TIMED) {
                // If we are using RMT, we can't delay here.
                delay_us(_directionDelayUsecs);
            }
        }
    }

    // Called from Axes::step() and, probably incorrectly, from UnipolarMotor::step()
    void IRAM_ATTR Stepping::startPulseTimer() {
        // Do not use switch() in IRAM
        if (_engine == stepper_id_t::I2S_STREAM) {
            // Generate the number of pulses needed to span pulse_microseconds
            i2s_out_push_sample(_pulseUsecs);
        } else if (_engine == stepper_id_t::I2S_STATIC) {
            i2s_out_push();
            _stepPulseEndTime = usToEndTicks(_pulseUsecs);
        } else if (_engine == stepper_id_t::TIMED) {
            _stepPulseEndTime = usToEndTicks(_pulseUsecs);
        }
    }

    // Called only from Axes::unstep()
    void IRAM_ATTR Stepping::finishPulse() {
        if (_engine == stepper_id_t::I2S_STATIC) {
            i2s_out_push();
        }
    }

    // Called only from Stepper::pulse_func when a new segment is loaded
    // The argument is in units of ticks of the timer that generates ISRs
    void IRAM_ATTR Stepping::setTimerPeriod(uint16_t timerTicks) {
        if (_engine == I2S_STREAM) {
            // Pulse ISR is called for each tick of alarm_val.
            // The argument to i2s_out_set_pulse_period is in units of microseconds
            i2s_out_set_pulse_period(((uint32_t)timerTicks) / ticksPerMicrosecond);
        } else {
            stepTimerSetTicks((uint32_t)timerTicks);
        }
    }

    // Called only from Stepper::wake_up which is not used in ISR context
    void Stepping::startTimer() {
        if (_engine == I2S_STREAM) {
            i2s_out_set_stepping();
        } else {
            stepTimerStart();
        }
    }
    // Called only from Stepper::stop_stepping, used in both ISR and foreground contexts
    void IRAM_ATTR Stepping::stopTimer() {
        if (_engine == I2S_STREAM) {
            i2s_out_set_passthrough();
        } else {
            stepTimerStop();
        }
    }

    void Stepping::group(Configuration::HandlerBase& handler) {
        handler.item("engine", _engine, stepTypes);
        handler.item("idle_ms", _idleMsecs, 0, 10000000);  // full range
        handler.item("pulse_us", _pulseUsecs, 0, 30);
        handler.item("dir_delay_us", _directionDelayUsecs, 0, 10);
        handler.item("disable_delay_us", _disableDelayUsecs, 0, 1000000);  // max 1 second
        handler.item("segments", _segments, 6, 20);
    }

    void Stepping::afterParse() {
        if (_engine == I2S_STREAM || _engine == I2S_STATIC) {
            Assert(config->_i2so, "I2SO bus must be configured for this stepping type");
            if (_pulseUsecs < I2S_OUT_USEC_PER_PULSE) {
                log_warn("Increasing stepping/pulse_us to the IS2 minimum value " << I2S_OUT_USEC_PER_PULSE);
                _pulseUsecs = I2S_OUT_USEC_PER_PULSE;
            }
            if (_engine == I2S_STREAM && _pulseUsecs > I2S_STREAM_MAX_USEC_PER_PULSE) {
                log_warn("Decreasing stepping/pulse_us to " << I2S_STREAM_MAX_USEC_PER_PULSE << ", the maximum value for I2S_STREAM");
                _pulseUsecs = I2S_STREAM_MAX_USEC_PER_PULSE;
            }
        }
    }

    uint32_t Stepping::maxPulsesPerSec() {
        switch (_engine) {
            case stepper_id_t::I2S_STREAM:
            case stepper_id_t::I2S_STATIC:
                return i2s_out_max_steps_per_sec;
            case stepper_id_t::RMT:
                return 1000000 / (2 * _pulseUsecs + _directionDelayUsecs);
            case stepper_id_t::TIMED:
            default:
                return 80000;  // based on testing
        }
    }
}
