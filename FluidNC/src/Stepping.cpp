#include "I2SOut.h"
#include "EnumItem.h"
#include "Stepping.h"
#include "Stepper.h"
#include "Machine/MachineConfig.h"  // config

#include <esp32-hal-gpio.h>
#include <driver/rmt.h>

#include <atomic>

namespace Machine {

    // fStepperTimer should be an integer divisor of the bus speed, i.e. of fTimers
    const int ticksPerMicrosecond = Stepping::fStepperTimer / 1000000;

    int Stepping::_engine = RMT_ENGINE;

    int Stepping::_n_active_axes = 0;

    bool    Stepping::_switchedStepper = false;
    int32_t Stepping::_stepPulseEndTime;
    size_t  Stepping::_segments = 12;

    int Stepping::_i2sPulseCounts = 2;

    uint32_t Stepping::_idleMsecs           = 255;
    uint32_t Stepping::_pulseUsecs          = 4;
    uint32_t Stepping::_directionDelayUsecs = 0;
    uint32_t Stepping::_disableDelayUsecs   = 0;

    const EnumItem stepTypes[] = { { Stepping::TIMED, "Timed" },
                                   { Stepping::RMT_ENGINE, "RMT" },
                                   { Stepping::I2S_STATIC, "I2S_static" },
                                   { Stepping::I2S_STREAM, "I2S_stream" },
                                   EnumItem(Stepping::RMT_ENGINE) };

    void Stepping::init() {
        log_info("Stepping:" << stepTypes[_engine].name << " Pulse:" << _pulseUsecs << "us Dsbl Delay:" << _disableDelayUsecs
                             << "us Dir Delay:" << _directionDelayUsecs << "us Idle Delay:" << _idleMsecs << "ms"
                             << " Pulses: " << _i2sPulseCounts);

        // Prepare stepping interrupt callbacks.  The one that is actually
        // used is determined by timerStart() and timerStop()

        // Setup a timer for direct stepping
        stepTimerInit(fStepperTimer, Stepper::pulse_func);

        // Register pulse_func with the I2S subsystem
        // This could be done via the linker.
        //        i2s_out_set_pulse_callback(Stepper::pulse_func);

        Stepper::init();
    }

    static int init_rmt_channel(int step_gpio, bool invert_step, uint32_t dir_delay_ms, uint32_t pulse_us) {
        static rmt_channel_t next_RMT_chan_num = RMT_CHANNEL_0;
        if (next_RMT_chan_num == RMT_CHANNEL_MAX) {
            log_error("Out of RMT channels");
            return -1;
        }
        rmt_channel_t rmt_chan_num = next_RMT_chan_num;
        next_RMT_chan_num          = static_cast<rmt_channel_t>(static_cast<int>(next_RMT_chan_num) + 1);

        rmt_config_t rmtConfig = { .rmt_mode      = RMT_MODE_TX,
                                   .channel       = rmt_chan_num,
                                   .gpio_num      = gpio_num_t(step_gpio),
                                   .clk_div       = 20,
                                   .mem_block_num = 2,
                                   .flags         = 0,
                                   .tx_config     = {
                                           .carrier_freq_hz      = 0,
                                           .carrier_level        = RMT_CARRIER_LEVEL_LOW,
                                           .idle_level           = invert_step ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW,
                                           .carrier_duty_percent = 50,
#if SOC_RMT_SUPPORT_TX_LOOP_COUNT
                                       .loop_count = 1,
#endif
                                       .carrier_en     = false,
                                       .loop_en        = false,
                                       .idle_output_en = true,
                                   } };

        rmt_item32_t rmtItem[2];
        rmtItem[0].duration0 = dir_delay_ms ? dir_delay_ms * 4 : 1;
        rmtItem[0].duration1 = 4 * pulse_us;
        rmtItem[1].duration0 = 0;
        rmtItem[1].duration1 = 0;

        rmtItem[0].level0 = rmtConfig.tx_config.idle_level;
        rmtItem[0].level1 = !rmtConfig.tx_config.idle_level;
        rmt_config(&rmtConfig);
        rmt_fill_tx_items(rmtConfig.channel, &rmtItem[0], rmtConfig.mem_block_num, 0);
        return static_cast<int>(rmt_chan_num);
    }

    Stepping::motor_t* Stepping::axis_motors[MAX_N_AXIS][MAX_MOTORS_PER_AXIS] = { nullptr };

    void Stepping::assignMotor(int axis, int motor, int step_pin, bool step_invert, int dir_pin, bool dir_invert) {
        if (axis >= _n_active_axes) {
            _n_active_axes = axis + 1;
        }
        if (_engine == RMT_ENGINE) {
            step_pin = init_rmt_channel(step_pin, step_invert, _directionDelayUsecs, _pulseUsecs);
        }

        motor_t* m               = new motor_t;
        axis_motors[axis][motor] = m;
        m->step_pin              = step_pin;
        m->step_invert           = step_invert;
        m->dir_pin               = dir_pin;
        m->dir_invert            = dir_invert;
        m->blocked               = false;
        m->limited               = false;
    }

    int Stepping::axis_steps[MAX_N_AXIS] = { 0 };

    bool* Stepping::limit_var(int axis, int motor) {
        auto m = axis_motors[axis][motor];
        return m ? &(m->limited) : nullptr;
    }

    void Stepping::block(int axis, int motor) {
        auto m = axis_motors[axis][motor];
        if (m) {
            m->blocked = true;
        }
    }

    void Stepping::unblock(int axis, int motor) {
        auto m = axis_motors[axis][motor];
        if (m) {
            m->blocked = false;
        }
    }

    void Stepping::limit(int axis, int motor) {
        auto m = axis_motors[axis][motor];
        if (m) {
            m->limited = true;
        }
    }
    void Stepping::unlimit(int axis, int motor) {
        auto m = axis_motors[axis][motor];
        if (m) {
            m->limited = false;
        }
    }

    void IRAM_ATTR Stepping::step(uint8_t step_mask, uint8_t dir_mask) {
        // Set the direction pins, but optimize for the common
        // situation where the direction bits haven't changed.
        static uint8_t previous_dir_mask = 255;  // should never be this value
        if (previous_dir_mask == 255) {
            // Set all the direction bits the first time
            previous_dir_mask = ~dir_mask;
        }

        if (dir_mask != previous_dir_mask) {
            for (size_t axis = 0; axis < _n_active_axes; axis++) {
                bool dir     = bitnum_is_true(dir_mask, axis);
                bool old_dir = bitnum_is_true(previous_dir_mask, axis);
                if (dir != old_dir) {
                    for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                        auto m = axis_motors[axis][motor];
                        if (m) {
                            int  pin       = m->dir_pin;
                            bool direction = dir ^ m->dir_invert;
                            if (_engine == RMT_ENGINE || _engine == TIMED) {
                                gpio_write(pin, direction);
                            } else if (_engine == I2S_STATIC || _engine == I2S_STREAM) {
                                i2s_out_write(pin, direction);
                            }
                        }
                    }
                }
                waitDirection();
            }
            previous_dir_mask = dir_mask;
        }

        // Turn on step pulses for motors that are supposed to step now
        for (size_t axis = 0; axis < _n_active_axes; axis++) {
            if (bitnum_is_true(step_mask, axis)) {
                auto increment = bitnum_is_true(dir_mask, axis) ? -1 : 1;
                axis_steps[axis] += increment;
                for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                    auto m = axis_motors[axis][motor];
                    if (m && !m->blocked && !m->limited) {
                        int  pin      = m->step_pin;
                        bool inverted = m->step_invert;
                        if (_engine == RMT_ENGINE) {
                            // Restart the RMT which has already been configured
                            // for the desired pulse length and polarity
#ifdef CONFIG_IDF_TARGET_ESP32
                            RMT.conf_ch[pin].conf1.mem_rd_rst = 1;
                            RMT.conf_ch[pin].conf1.mem_rd_rst = 0;
                            RMT.conf_ch[pin].conf1.tx_start   = 1;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
                            RMT.chnconf0[pin].mem_rd_rst_n = 1;
                            RMT.chnconf0[pin].mem_rd_rst_n = 0;
                            RMT.chnconf0[pin].tx_start_n   = 1;
#endif
                        } else if (_engine == I2S_STATIC || _engine == I2S_STREAM) {
                            i2s_out_write(pin, !inverted);
                        } else if (_engine == TIMED) {
                            gpio_write(pin, !inverted);
                        }
                    }
                }
            }
        }
        // Do not use switch() in IRAM
        if (_engine == stepper_id_t::I2S_STREAM) {
            // Generate the number of pulses needed to span pulse_microseconds
            i2s_out_push_sample(_pulseUsecs);
        } else if (_engine == stepper_id_t::I2S_STATIC) {
            // i2s_out_push();
            for (int i = 0; i < _i2sPulseCounts; i++) {
                i2s_out_push_fifo();
            }
#if 0
            _stepPulseEndTime = usToEndTicks(_pulseUsecs);
#endif
        } else if (_engine == stepper_id_t::TIMED) {
            _stepPulseEndTime = usToEndTicks(_pulseUsecs);
        }
    }

    // Turn all stepper pins off
    void IRAM_ATTR Stepping::unstep() {
        // With RMT, the end of the step is automatic
        if (_engine == RMT_ENGINE) {
            return;
        }
#if 0
        if (_engine == I2S_STATIC || _engine == TIMED) {  // Wait pulse
            spinUntil(_stepPulseEndTime);
        }
#endif
        for (size_t axis = 0; axis < _n_active_axes; axis++) {
            for (size_t motor = 0; motor < MAX_MOTORS_PER_AXIS; motor++) {
                auto m = axis_motors[axis][motor];
                if (m) {
                    int  pin      = m->step_pin;
                    bool inverted = m->step_invert;
                    if (_engine == I2S_STATIC || _engine == I2S_STREAM) {
                        i2s_out_write(pin, inverted);
                    } else if (_engine == TIMED) {
                        gpio_write(pin, inverted);
                    }
                }
            }
        }
        if (_engine == stepper_id_t::I2S_STATIC) {
            // i2s_out_push();
            i2s_out_push_fifo();
        }
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

    // Called only from step()
    void IRAM_ATTR Stepping::waitDirection() {
        if (_directionDelayUsecs) {
            // Stepper drivers need some time between changing direction and doing a pulse.
            // Do not use switch() in IRAM
            if (_engine == stepper_id_t::I2S_STREAM) {
                // Commit the pin changes to the DMA queue
                i2s_out_push_sample(_directionDelayUsecs);
            } else if (_engine == stepper_id_t::I2S_STATIC) {
                // Commit the pin changes to the hardware immediately
                // i2s_out_push();
                i2s_out_push_fifo();
                delay_us(_directionDelayUsecs);
            } else if (_engine == stepper_id_t::TIMED) {
                // If we are using RMT, we can't delay here.
                delay_us(_directionDelayUsecs);
            }
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
            if ((_engine == I2S_STATIC || _engine == I2S_STREAM) && _pulseUsecs > I2S_STREAM_MAX_USEC_PER_PULSE) {
                log_warn("Decreasing stepping/pulse_us to " << I2S_STREAM_MAX_USEC_PER_PULSE << ", the maximum value for I2S_STREAM");
                _pulseUsecs = I2S_STREAM_MAX_USEC_PER_PULSE;
            }
        }
        if (_engine == I2S_STATIC || _engine == I2S_STREAM) {
            // Number of I2S frames for a pulse, rounded up
            _i2sPulseCounts = (_pulseUsecs + I2S_OUT_USEC_PER_PULSE - 1) / I2S_OUT_USEC_PER_PULSE;
        }
    }

    uint32_t Stepping::maxPulsesPerSec() {
        switch (_engine) {
            case stepper_id_t::I2S_STREAM:
            case stepper_id_t::I2S_STATIC:
                return 1000000 / ((_i2sPulseCounts + 1) * I2S_OUT_USEC_PER_PULSE);
            case stepper_id_t::RMT_ENGINE:
                return 1000000 / (2 * _pulseUsecs + _directionDelayUsecs);
            case stepper_id_t::TIMED:
            default:
                return 80000;  // based on testing
        }
    }
}
