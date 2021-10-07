// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../PinMapper.h"

#include <cstdint>

const float TMC2130_RSENSE_DEFAULT = 0.11f;
const float TMC5160_RSENSE_DEFAULT = 0.075f;

const int NORMAL_TCOOLTHRS = 0xFFFFF;  // 20 bit is max
const int NORMAL_THIGH     = 0;

class TMC2130Stepper;  // Forward declaration
class TMC5160Stepper;  // Forward declaration

namespace MotorDrivers {

    class TrinamicSpiDriver : public TrinamicBase {
    private:
        const int _spi_freq = 100000;

        static pinnum_t daisy_chain_cs_id;
        static uint8_t  spi_index_mask;

        // It is really tempting to have a single pointer here because
        // TMC2130 and TMC5160 share many methods with the same names
        // and API.  That does not work because the common methods are
        // not virtual and their respective implementations are
        // incompatible due to hardware differences.  Therefore it is
        // necessary to preserve the full type knowledge in the pointers.
        TMC2130Stepper* tmc2130 = nullptr;
        TMC5160Stepper* tmc5160 = nullptr;

        Pin       _cs_pin;  // The chip select pin (can be the same for daisy chain)
        PinMapper _cs_mapping;
        int32_t   _spi_index = -1;

        bool test();
        void set_mode(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);

    protected:
        void config_message() override;

    public:
        TrinamicSpiDriver(uint16_t driver_part_number);

        // Overrides for inherited methods
        void init() override;
        void read_settings() override;
        bool set_homing_mode(bool ishoming) override;
        void set_disable(bool disable) override;

        void config_motor() override;

        void debug_message();

        // Configuration handlers:
        void afterParse() override {
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

        void validate() const override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("cs_pin", _cs_pin);
            handler.item("spi_index", _spi_index);
            TrinamicBase::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "trinamic_spi"; }
    };

    class TMC2130 : public TrinamicSpiDriver {
    public:
        TMC2130() : TrinamicSpiDriver(2130) {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_2130"; }
    };

    class TMC5160 : public TrinamicSpiDriver {
    public:
        TMC5160() : TrinamicSpiDriver(5160) {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_5160"; }
    };
}
