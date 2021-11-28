#include "I2SOut.h"
#include "EnumItem.h"
#include "Stepping.h"
#include "Stepper.h"
#include "Machine/MachineConfig.h"  // config

#include <atomic>

namespace Machine {

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

        const bool isEdge  = false;
        const bool countUp = true;

        // Setup a timer for direct stepping
        stepTimer = timerBegin(stepTimerNumber, fTimers / fStepperTimer, countUp);
        timerAttachInterrupt(stepTimer, onStepperDriverTimer, isEdge);

        // Register pulse_func with the I2S subsystem
        // This could be done via the linker.
        //        i2s_out_set_pulse_callback(Stepper::pulse_func);
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
            // 1 tick = fTimers / fStepperTimer
            // Pulse ISR is called for each tick of alarm_val.
            // The argument to i2s_out_set_pulse_period is in units of microseconds
            i2s_out_set_pulse_period(((uint32_t)timerTicks) / ticksPerMicrosecond);
        } else {
            timerAlarmWrite(stepTimer, (uint64_t)timerTicks, false);  // false disables autoreload
        }
    }
    // Called only from Stepper::wake_up which is not used in ISR context
    void Stepping::startTimer() {
        if (_engine == I2S_STREAM) {
            i2s_out_set_stepping();
        } else {
            timerWrite(stepTimer, 0ULL);
            timerAlarmEnable(stepTimer);
        }
    }
    // Called only from Stepper::stop_stepping, used in both ISR and foreground contexts
    void IRAM_ATTR Stepping::stopTimer() {
        if (_engine == I2S_STREAM) {
            i2s_out_set_passthrough();
        } else if (stepTimer) {
            timerAlarmDisable(stepTimer);
        }
    }

    // Stepper timer configuration
    hw_timer_t* Stepping::stepTimer = nullptr;  // Handle

    // Counts stepper ISR invocations.  This variable can be inspected
    // from the mainline code to determine if the stepper ISR is running,
    // since printing from the ISR is not a good idea.
    uint32_t Stepping::isr_count = 0;

    // Used to avoid ISR nesting of the "Stepper Driver Interrupt". Should never occur though.
    // TODO: Replace direct updating of the int32 position counters in the ISR somehow. Perhaps use smaller
    // int8 variables and update position counters only when a segment completes. This can get complicated
    // with probing and homing cycles that require true real-time positions.
    void IRAM_ATTR Stepping::onStepperDriverTimer() {
        // Timer ISR, normally takes a step.
        //
        // The intermediate handler clears the timer interrupt so we need not do it here
        ++isr_count;

        timerWrite(stepTimer, 0ULL);

        // It is tempting to defer this until after pulse_func(),
        // but if pulse_func() determines that no more stepping
        // is required and disables the timer, then that will be undone
        // if the re-enable happens afterwards.

        timerAlarmEnable(stepTimer);

        Stepper::pulse_func();
    }

    void Stepping::group(Configuration::HandlerBase& handler) {
        handler.item("engine", _engine, stepTypes);
        handler.item("idle_ms", _idleMsecs);
        handler.item("pulse_us", _pulseUsecs);
        handler.item("dir_delay_us", _directionDelayUsecs);
        handler.item("disable_delay_us", _disableDelayUsecs);
    }

    void Stepping::afterParse() {
        if (_engine == I2S_STREAM || _engine == I2S_STATIC) {
            Assert(config->_i2so, "I2SO bus must be configured for this stepping type");
            if (_pulseUsecs < I2S_OUT_USEC_PER_PULSE) {
                log_info("Increasing stepping/pulse_us to the IS2 minimum value " << I2S_OUT_USEC_PER_PULSE);
                _pulseUsecs = I2S_OUT_USEC_PER_PULSE;
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
