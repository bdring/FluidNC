// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicUartDriver.h"
#include "TMC2209SharedAddress.h"
#include "Pin.h"
#include "PinMapper.h"

#include <algorithm>
#include <cstdint>
#include <vector>

const float TMC2209_RSENSE_DEFAULT = 0.11f;

namespace MotorDrivers {

    class TMC2209Driver : public TrinamicUartDriver {
    public:
        TMC2209Driver(const char* name) : TrinamicUartDriver(name) { _tmc2209_instances.push_back(this); }
        ~TMC2209Driver() override {
            _tmc2209_instances.erase(std::remove(_tmc2209_instances.begin(), _tmc2209_instances.end(), this), _tmc2209_instances.end());
        }

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override;

        void group(Configuration::HandlerBase& handler) override {
            TrinamicUartDriver::group(handler);

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

            // @config homing_amps
            // @default 0.0 -- substituted with run_amps if left at 0
            // Motor current while homing. Leaving this at its default 0 isn't literally
            // "zero current" -- afterParse() detects the default and substitutes run_amps
            // instead, so omitting this field entirely is equivalent to setting it equal to
            // run_amps. This fallback is specific to TMC2209; no other Trinamic driver type
            // has a homing_amps field at all.
            handler.item("homing_amps", _homing_current, 0.0, 10.0);

            // @config stallguard
            // @default 0
            // StallGuard sensitivity threshold, 0 (least sensitive) to 255 (most
            // sensitive) -- note this range is different from every SPI-driven Trinamic
            // type (tmc_2130/tmc_5160/etc.), which use -64 to 63 instead. Only meaningful
            // when run_mode or homing_mode is StallGuard.
            handler.item("stallguard", _stallguard, 0, 255);

            // @config stallguard_debug
            // @default false
            // Logs live StallGuard sensor values -- useful for tuning the stallguard
            // threshold for sensorless homing. Not usable together with
            // shared_address_write_only.
            handler.item("stallguard_debug", _stallguardDebugMode);

            // @config toff_coolstep
            // @default 3
            // TOFF (off-time) register value used in CoolStep/StallGuard mode.
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);

            // @config shared_address_write_only
            // @default false
            // Acknowledges that this chip's UART address (uart_num + addr) is intentionally
            // shared with other TMC2209 motors on the same bus rather than uniquely
            // assigned -- required on every driver sharing that address, or validation
            // fails with a "must set shared_address_write_only: true" error. Since replies
            // can't be distinguished on a shared address, this also requires cs_pin: NO_PIN
            // and disallows stallguard_debug, and every driver sharing the address must
            // agree on the same current/microstep/mode settings (a mismatch is a validation
            // error, not a silent inconsistency).
            handler.item("shared_address_write_only", _shared_address_write_only);
        }

        void afterParse() override {
            TrinamicUartDriver::afterParse();
            if (_homing_current == 0) {
                _homing_current = _run_current;
            }
        }

    private:
        static std::vector<TMC2209Driver*> _tmc2209_instances;

        TMC2209Stepper* tmc2209 = nullptr;
        bool            _shared_address_write_only = false;

        bool test();
        void set_registers(bool isHoming);
        bool sameUartAddress(const TMC2209Driver& other) const;
        TMC2209UartSettings uartSettings() const;
    };
}
