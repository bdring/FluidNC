// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    DacSpindle.cpp

    This uses the Analog DAC to generate a voltage
    proportional to the GCode S value desired. Some spindle uses
    a 0-5V or 0-10V value to control the spindle. You might need an
    external Op Amp type circuit to upconvert the MCU pin voltage.
*/
#include "Config.h"
#if MAX_N_DACS

#    include "DacSpindle.h"

#    include <driver/dac_oneshot.h>  // dac_oneshot_new_channel
#    include <hal/dac_ll.h>           // dac_ll_update_output_value
#    include <soc/dac_channel.h>      // DAC_CHAN0_GPIO_NUM

namespace Spindles {
    // ======================================== Dac ======================================
    void Dac::init() {
        if (_output_pin.undefined()) {
            return;
        }

        if (!_output_pin.capabilities().has(Pin::Capabilities::DAC)) {  // DAC can only be used on these pins
            log_error("DAC spindle pin invalid " << _output_pin.name() << " (pin 25 or 26 only)");
            return;
        }

        // The driver sets up the pin and powers the channel once, here;
        // set_output() then writes the register directly, since it runs
        // in the step ISR, where the driver's calls are not allowed.
        _dac_chan = _output_pin.getNative(Pin::Capabilities::DAC) == DAC_CHAN0_GPIO_NUM ? DAC_CHAN_0 : DAC_CHAN_1;
        dac_oneshot_config_t config = { .chan_id = dac_channel_t(_dac_chan) };
        if (dac_oneshot_new_channel(&config, &_dac_handle) != ESP_OK) {
            _dac_handle = nullptr;
            log_error("DAC spindle cannot use " << _output_pin.name());
            return;
        }
        _gpio_ok = true;

        _enable_pin.setAttr(Pin::Attr::Output);
        _direction_pin.setAttr(Pin::Attr::Output);

        is_reversable = _direction_pin.defined();

        if (_speeds.size() == 0) {
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(255);

        init_atc();
        config_message();
    }

    void Dac::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Dir:" << _direction_pin.name()
                        << " Res:8bits" << atc_info());
    }

    void IRAM_ATTR Dac::setSpeedfromISR(uint32_t speed) {
        set_output(speed);
    };
    void IRAM_ATTR Dac::set_output(uint32_t duty) {
        if (_gpio_ok) {
            dac_ll_update_output_value(dac_channel_t(_dac_chan), static_cast<uint8_t>(duty));
        }
    }

    Dac::~Dac() {
        if (_dac_handle) {
            dac_oneshot_del_channel(_dac_handle);
        }
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Dac> registration("DAC");
    }

    // Called from the step ISR in place of a virtual dispatch, which would have
    // to read this class's vtable out of flash.  IRAM_ATTR, and the qualified
    // call keeps it non-virtual.
    static void IRAM_ATTR dac_speed_thunk(Spindle* s, uint32_t dev_speed) {
        static_cast<Dac*>(s)->Dac::setSpeedfromISR(dev_speed);
    }
    Spindle::IsrSpeedFn Dac::isr_speed_fn() {
        return dac_speed_thunk;
    }
}
#endif
