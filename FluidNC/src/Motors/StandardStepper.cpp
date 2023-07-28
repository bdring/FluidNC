// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for stepper motors that just require step and direction pins.
*/

#include "StandardStepper.h"

#include "../Machine/MachineConfig.h"
#include "../Stepper.h"   // ST_I2S_*
#include "../Stepping.h"  // config->_stepping->_engine

#include <esp32-hal-gpio.h>  // gpio
#include <sdkconfig.h>       // CONFIG_IDF_TARGET_*

using namespace Machine;

namespace MotorDrivers {

    static void init_rmt_channel(rmt_channel_t& rmt_chan_num, Pin& step_pin, bool invert_step, uint32_t dir_delay_ms, uint32_t pulse_us) {
        static rmt_channel_t next_RMT_chan_num = RMT_CHANNEL_0;
        if (rmt_chan_num == RMT_CHANNEL_MAX) {
            if (next_RMT_chan_num == RMT_CHANNEL_MAX) {
                log_error("Out of RMT channels");
                return;
            }
            rmt_chan_num      = next_RMT_chan_num;
            next_RMT_chan_num = static_cast<rmt_channel_t>(static_cast<int>(next_RMT_chan_num) + 1);
        }

        auto step_pin_gpio = step_pin.getNative(Pin::Capabilities::Output);

        rmt_config_t rmtConfig = { .rmt_mode      = RMT_MODE_TX,
                                   .channel       = rmt_chan_num,
                                   .gpio_num      = gpio_num_t(step_pin_gpio),
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
    }

    void StandardStepper::init() {
        read_settings();
        config_message();
    }

    void StandardStepper::read_settings() { init_step_dir_pins(); }

    void StandardStepper::init_step_dir_pins() {
        auto axisIndex = axis_index();

        _invert_step    = _step_pin.getAttr().has(Pin::Attr::ActiveLow);
        _invert_disable = _disable_pin.getAttr().has(Pin::Attr::ActiveLow);

        _dir_pin.setAttr(Pin::Attr::Output);

        auto stepping = config->_stepping;
        if (stepping->_engine == Stepping::RMT) {
            init_rmt_channel(_rmt_chan_num, _step_pin, _invert_step, stepping->_directionDelayUsecs, stepping->_pulseUsecs);
        } else {
            _step_pin.setAttr(Pin::Attr::Output);
        }

        if (_disable_pin.defined()) {
            _disable_pin.setAttr(Pin::Attr::Output);
        }
    }

    void StandardStepper::config_message() {
        log_info("    " << name() << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " Disable:" << _disable_pin.name());
    }

    void IRAM_ATTR StandardStepper::step() {
        if (config->_stepping->_engine == Stepping::RMT && _rmt_chan_num != RMT_CHANNEL_MAX) {
#ifdef CONFIG_IDF_TARGET_ESP32
            RMT.conf_ch[_rmt_chan_num].conf1.mem_rd_rst = 1;
            RMT.conf_ch[_rmt_chan_num].conf1.mem_rd_rst = 0;
            RMT.conf_ch[_rmt_chan_num].conf1.tx_start   = 1;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
            RMT.chnconf0[_rmt_chan_num].mem_rd_rst_n = 1;
            RMT.chnconf0[_rmt_chan_num].mem_rd_rst_n = 0;
            RMT.chnconf0[_rmt_chan_num].tx_start_n   = 1;
#endif
        } else {
            _step_pin.on();
        }
    }

    void IRAM_ATTR StandardStepper::unstep() {
        if (config->_stepping->_engine != Stepping::RMT) {
            _step_pin.off();
        }
    }

    void IRAM_ATTR StandardStepper::set_direction(bool dir) { _dir_pin.write(dir); }

    void IRAM_ATTR StandardStepper::set_disable(bool disable) { _disable_pin.synchronousWrite(disable); }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<StandardStepper> registration("standard_stepper");
    }

    void StandardStepper::validate() {
        Assert(_step_pin.defined(), "Step pin must be configured.");
        bool isI2SO = config->_stepping->_engine == Stepping::I2S_STREAM || config->_stepping->_engine == Stepping::I2S_STATIC;
        if (isI2SO) {
            Assert(_step_pin.name().rfind("I2SO", 0) == 0, "Step pin must be an I2SO pin");
            if (_dir_pin.defined()) {
                Assert(_dir_pin.name().rfind("I2SO", 0) == 0, "Direction pin must be an I2SO pin");
            }

        } else {
            Assert(_step_pin.name().rfind("gpio", 0) == 0, "Step pin must be a GPIO pin");
            if (_dir_pin.defined()) {
                Assert(_dir_pin.name().rfind("gpio", 0) == 0, "Direction pin must be a GPIO pin");
            }
        }
    }
}
