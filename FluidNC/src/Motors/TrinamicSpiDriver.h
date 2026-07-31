// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const int NORMAL_TCOOLTHRS = 0xFFFFF;  // 20 bit is max
const int NORMAL_THIGH     = 0;

namespace MotorDrivers {

    class TrinamicSpiDriver : public TrinamicBase {
    public:
        TrinamicSpiDriver(const char* name) : TrinamicBase(name) {}
        TrinamicSpiDriver() = default;

        // Overrides for inherited methods
        virtual void init() override;
        //bool         set_homing_mode(bool ishoming) override;

        // Configuration handlers:
        void afterParse() override {
            if (!_spi_setup_done) {
                if (daisy_chain_cs_id == INVALID_PINNUM) {
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

            // @config cs_pin
            // @default NO_PIN
            // SPI chip-select for this driver. In independent (non-daisy-chained) SPI mode
            // each driver needs its own; in a daisy chain, define this only on the motor
            // with spi_index: 1 -- the rest share that same physical CS line.
            handler.item("cs_pin", _cs_pin);

            // @config spi_index
            // @default -1
            // -1 means independent SPI mode (used on all drivers when not daisy-chaining).
            // In a daisy chain, each driver gets a distinct position number (1, 2, 3, ...)
            // in chain order -- every physical position in the chain must be represented by
            // a motor entry, even unused ones, or the chain's data alignment breaks.
            handler.item("spi_index", _spi_index, -1, 127);

            // @config run_mode
            // @default StealthChop
            // Chopper algorithm while running: StealthChop (very quiet), CoolStep (runs
            // cooler, allows higher current), or StallGuard (CoolStep plus stall/load
            // detection).
            handler.item("run_mode", _run_mode, trinamicModes);

            // @config homing_mode
            // @default StealthChop
            // Chopper algorithm while homing (same choices as run_mode) -- StallGuard is
            // typically used here for sensorless homing.
            handler.item("homing_mode", _homing_mode, trinamicModes);

            // @config stallguard
            // @default 0
            // StallGuard sensitivity threshold for this SPI-driven chip family, -64
            // (most sensitive) to 63 (least sensitive). Only meaningful when run_mode or
            // homing_mode is StallGuard.
            handler.item("stallguard", _stallguard, -64, 63);

            // @config stallguard_debug
            // @default false
            // Logs live StallGuard sensor values -- useful for tuning the stallguard
            // threshold for sensorless homing.
            handler.item("stallguard_debug", _stallguardDebugMode);

            // @config toff_coolstep
            // @default 3
            // TOFF (off-time) register value used in CoolStep/StallGuard mode.
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);

            // @config diag0_error
            // @default false
            // Enables the DIAG0 pin to signal driver error conditions. SPI-driver-specific
            // -- not available on the UART-controlled Trinamic drivers.
            handler.item("diag0_error", _diag0_error);

            // @config diag0_otpw
            // @default false
            // Enables the DIAG0 pin to signal an over-temperature pre-warning.
            // SPI-driver-specific -- not available on the UART-controlled Trinamic drivers.
            handler.item("diag0_otpw", _diag0_otpw);

            // @config diag0_int_pushpull
            // @default false
            // Configures the DIAG0 pin's output stage as push-pull instead of open-drain.
            // SPI-driver-specific -- not available on the UART-controlled Trinamic drivers.
            handler.item("diag0_int_pushpull", _diag0_int_pushpull);
        }

    protected:
        Pin     _cs_pin;  // The chip select pin (can be the same for daisy chain)
        int32_t _spi_index      = -1;
        bool    _spi_setup_done = false;

        bool _diag0_error        = false;
        bool _diag0_otpw         = false;
        bool _diag0_int_pushpull = false;

        static constexpr int _spi_freq = 100000;

        void config_message() override;

        pinnum_t setupSPI();

        bool    reportTest(uint8_t result);
        uint8_t toffValue();

    private:
        static pinnum_t daisy_chain_cs_id;
        static uint8_t  spi_index_mask;

        PinMapper _cs_mapping;
    };

}
