/*
    StandardStepper.cpp

    This is used for a stepper motor that just requires step and direction
    pins.
    TODO: Add an enable pin

    Part of Grbl_ESP32

    2020 -	Bart Dring

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "StandardStepper.h"

#include "../Machine/MachineConfig.h"
#include "../Stepper.h"   // ST_I2S_*
#include "../Stepping.h"  // config->_stepping->_engine

#include <esp32-hal-gpio.h>  // gpio

using namespace Machine;

namespace MotorDrivers {
    rmt_item32_t StandardStepper::rmtItem[2];
    rmt_config_t StandardStepper::rmtConfig;

    // Get an RMT channel number
    // returns RMT_CHANNEL_MAX for error
    rmt_channel_t StandardStepper::get_next_RMT_chan_num() {
        static uint8_t next_RMT_chan_num = uint8_t(RMT_CHANNEL_0);  // channels 0-7 are valid
        if (next_RMT_chan_num < RMT_CHANNEL_MAX) {
            next_RMT_chan_num++;
        } else {
            log_error("Out of RMT channels");
        }
        return rmt_channel_t(next_RMT_chan_num);
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
            rmtConfig.rmt_mode                       = RMT_MODE_TX;
            rmtConfig.clk_div                        = 20;
            rmtConfig.mem_block_num                  = 2;
            rmtConfig.tx_config.loop_en              = false;
            rmtConfig.tx_config.carrier_en           = false;
            rmtConfig.tx_config.carrier_freq_hz      = 0;
            rmtConfig.tx_config.carrier_duty_percent = 50;
            rmtConfig.tx_config.carrier_level        = RMT_CARRIER_LEVEL_LOW;
            rmtConfig.tx_config.idle_output_en       = true;

            rmtItem[0].duration0 = stepping->_directionDelayUsecs ? stepping->_directionDelayUsecs * 4 : 1;
            rmtItem[0].duration1 = 4 * stepping->_pulseUsecs;
            rmtItem[1].duration0 = 0;
            rmtItem[1].duration1 = 0;

            _rmt_chan_num = get_next_RMT_chan_num();
            if (_rmt_chan_num == RMT_CHANNEL_MAX) {
                return;
            }

            auto step_pin_gpio = _step_pin.getNative(Pin::Capabilities::Output);
            rmt_set_source_clk(_rmt_chan_num, RMT_BASECLK_APB);
            rmtConfig.channel              = _rmt_chan_num;
            rmtConfig.tx_config.idle_level = _invert_step ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW;
            rmtConfig.gpio_num             = gpio_num_t(step_pin_gpio);
            rmtItem[0].level0              = rmtConfig.tx_config.idle_level;
            rmtItem[0].level1              = !rmtConfig.tx_config.idle_level;
            rmt_config(&rmtConfig);
            rmt_fill_tx_items(rmtConfig.channel, &rmtItem[0], rmtConfig.mem_block_num, 0);
        } else {
            _step_pin.setAttr(Pin::Attr::Output);
        }

        _disable_pin.setAttr(Pin::Attr::Output);
    }

    void StandardStepper::config_message() {
        log_info("    Standard Stepper Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " Disable:" << _disable_pin.name() << " "
                                              << axisLimits());
    }

    void IRAM_ATTR StandardStepper::step() {
        if (config->_stepping->_engine == Stepping::RMT) {
            RMT.conf_ch[_rmt_chan_num].conf1.mem_rd_rst = 1;
            RMT.conf_ch[_rmt_chan_num].conf1.tx_start   = 1;
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

    void StandardStepper::validate() const {
        Assert(_step_pin.defined(), "Step pin must be configured.");
        Assert(_dir_pin.defined(), "Direction pin must be configured.");
        bool isI2SO = config->_stepping->_engine == Stepping::I2S_STREAM || config->_stepping->_engine == Stepping::I2S_STATIC;
        if (isI2SO) {
            Assert(_step_pin.name().startsWith("I2SO"), "Step pin must be an I2SO pin");
            Assert(_dir_pin.name().startsWith("I2SO"), "Direction pin must be an I2SO pin");
        } else {
            Assert(_step_pin.name().startsWith("gpio"), "Step pin must be a GPIO pin");
            Assert(_dir_pin.name().startsWith("gpio"), "Direction pin must be a GPIO pin");
        }
    }

}
