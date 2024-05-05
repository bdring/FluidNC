// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../PinMapper.h"

#include <cstdint>

const int NORMAL_TCOOLTHRS = 0xFFFFF;  // 20 bit is max
const int NORMAL_THIGH     = 0;

namespace MotorDrivers {

    class TrinamicSpiDriver : public TrinamicBase {
    public:
        TrinamicSpiDriver() = default;

        // Overrides for inherited methods
        virtual void init() override;
        //bool         set_homing_mode(bool ishoming) override;

        // Configuration handlers:
        void afterParse() override {
            if (!_spi_setup_done) {
                if (daisy_chain_cs_id == 255) {
                    // Either it is not a daisy chain or this is the first daisy-chained TMC in the config file
                    Assert(_cs_pin.defined(), "TMC cs_pin: pin must be configured");
                    if (_spi_index != -1) {
                        // This is the first daisy-chained TMC in the config file
                        // Do the cs pin mapping now and record the ID in daisy_chain_cs_id
                        _cs_pin.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
                        _cs_mapping       = PinMapper(_cs_pin);
                        daisy_chain_cs_id = _cs_mapping.pinId();
                        set_bitnum(spi_index_mask, _spi_index);
                    } else {
                        // The TMC SPI is not daisy-chained
                    }
                } else {
                    // This is another - not the first - daisy-chained TMC
                    Assert(_cs_pin.undefined(), "For daisy-chained TMC, cs_pin: pin must be configured only once");
                    Assert(_spi_index != -1, "spi_index: must be configured on all daisy-chained TMCs");
                    Assert(bitnum_is_false(spi_index_mask, _spi_index), "spi_index: must be unique among all daisy-chained TMCs");
                    set_bitnum(spi_index_mask, _spi_index);
                }
            }
            _spi_setup_done = true;
        }

        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicBase::group(handler);

            handler.item("cs_pin", _cs_pin);
            handler.item("spi_index", _spi_index, -1, 127);

            handler.item("run_mode", _run_mode, trinamicModes);
            handler.item("homing_mode", _homing_mode, trinamicModes);
            handler.item("stallguard", _stallguard, -64, 63);
            handler.item("stallguard_debug", _stallguardDebugMode);
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);
        }

    protected:
        Pin       _cs_pin;  // The chip select pin (can be the same for daisy chain)
        int32_t   _spi_index      = -1;
        bool      _spi_setup_done = false;

        static constexpr int _spi_freq = 100000;

        void config_message() override;

        uint8_t setupSPI();

        bool    reportTest(uint8_t result);
        uint8_t toffValue();

    private:
        static pinnum_t daisy_chain_cs_id;
        static uint8_t  spi_index_mask;

        PinMapper _cs_mapping;
    };

}
