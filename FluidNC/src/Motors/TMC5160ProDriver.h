// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicSpiDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC5160_RSENSE_DEFAULT = 0.075f;

/* 
Dump from an existing setup
[MSG:INFO: CHOPCONF: 0x13408158] 322994520
[MSG:INFO: COOLCONF: 0x0]
[MSG:INFO: THIGH: 0x0]
[MSG:INFO: TCOOLTHRS: 0x0]
[MSG:INFO: GCONF: 0x4] 4
[MSG:INFO: PWMCONF: 0xc40c001e] // 20468989982
[MSG:INFO: IHOLD_IRUN: 0x1f0c] // 7948

*/

namespace MotorDrivers {

    class TMC5160ProDriver : public TrinamicSpiDriver {
    public:
        TMC5160ProDriver(const char* name) : TrinamicSpiDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            // Expert/raw-register mode: unlike every other Trinamic driver type, this class
            // skips TrinamicBase/TrinamicSpiDriver's group() entirely (no run_amps/
            // hold_amps/microsteps/run_mode/etc.) and calls StandardStepper::group()
            // directly, then registers only the fields below. This class also backs the
            // tmc_2160Pro and tmc_2160 config names -- both register the same underlying
            // driver class with no differences, so all three names are functionally
            // interchangeable; pick whichever best documents the actual chip in a config.
            //handler.item("tpfd", _tpfd, 0, 15);
            StandardStepper::group(handler);

            // @config cs_pin
            // @default NO_PIN
            // SPI chip-select for this driver. In independent (non-daisy-chained) SPI mode
            // each driver needs its own; in a daisy chain, define this only on the motor
            // with spi_index: 1.
            handler.item("cs_pin", _cs_pin);

            // @config spi_index
            // @default -1
            // -1 means independent SPI mode. In a daisy chain, each driver gets a distinct
            // position number (1, 2, 3, ...) in chain order.
            handler.item("spi_index", _spi_index, -1, 127);

            // @config use_enable
            // @default false
            // Uses disable_pin as an active enable signal (inverted sense) instead of the
            // ordinary active-disable sense.
            handler.item("use_enable", _use_enable);

            // @config CHOPCONF
            // @default 322994520
            // Raw TMC5160 CHOPCONF register value. Consult the TMC5160 datasheet -- these
            // 7 register fields are not semantic settings (no run_amps/microsteps/etc. here
            // at all), just the literal register contents applied at init.
            handler.item("CHOPCONF", CHOPCONF);

            // @config COOLCONF
            // @default 0
            // Raw TMC5160 COOLCONF register value.
            handler.item("COOLCONF", COOLCONF);

            // @config THIGH
            // @default 0
            // Raw TMC5160 THIGH register value.
            handler.item("THIGH", THIGH);

            // @config TCOOLTHRS
            // @default 0
            // Raw TMC5160 TCOOLTHRS register value.
            handler.item("TCOOLTHRS", TCOOLTHRS);

            // @config GCONF
            // @default 4
            // Raw TMC5160 GCONF register value.
            handler.item("GCONF", GCONF);

            // @config PWMCONF
            // @default 3289120798
            // Raw TMC5160 PWMCONF register value.
            handler.item("PWMCONF", PWMCONF);

            // @config IHOLD_IRUN
            // @default 7948
            // Raw TMC5160 IHOLD_IRUN register value (packs both hold and run current
            // directly, unlike the semantic run_amps/hold_amps fields used by tmc_5160).
            handler.item("IHOLD_IRUN", IHOLD_IRUN);
        }

    private:
        TMC5160Stepper* tmc5160 = nullptr;

        uint32_t CHOPCONF   = 322994520;
        uint32_t COOLCONF   = 0;
        uint32_t THIGH      = 0;
        uint32_t TCOOLTHRS  = 0;
        uint32_t GCONF      = 4;
        uint32_t PWMCONF    = 3289120798;
        uint32_t IHOLD_IRUN = 7948;

        bool test();
        void set_registers(bool isHoming);
    };
}
