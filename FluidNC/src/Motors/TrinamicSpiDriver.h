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

namespace MotorDrivers {

    class TrinamicSpiDriver : public TrinamicBase {
    private:
        const int _spi_freq = 100000;

        static uint8_t daisy_chain_cs;

        TMC2130Stepper* tmcstepper;  // all other driver types are subclasses of this one
        Pin             _cs_pin;     // The chip select pin (can be the same for daisy chain)
        PinMapper       _cs_mapping;
        int32_t          _spi_index;

        bool test();
        void set_mode(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);

    protected:
        void config_message() override;

    public:
        TrinamicSpiDriver(uint16_t driver_part_number) : TrinamicSpiDriver(driver_part_number, -1) {}

        TrinamicSpiDriver(uint16_t driver_part_number, int8_t spi_index);

        // Overrides for inherited methods
        void init() override;
        void read_settings() override;
        bool set_homing_mode(bool ishoming) override;
        void set_disable(bool disable) override;

        void debug_message();

        // Configuration handlers:
        void validate() const override {
            Assert(!_cs_pin.undefined(), "TMC spi_cs pin should be configured.");
            StandardStepper::validate();
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("cs", _cs_pin);
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
