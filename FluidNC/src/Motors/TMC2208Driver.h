// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicUartDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC2208_RSENSE_DEFAULT = 0.11f;

namespace MotorDrivers {

    class TMC2208Driver : public TrinamicUartDriver {
    public:
        TMC2208Driver(const char* name) : TrinamicUartDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicUartDriver::group(handler);

            // TMC2208 chips have no hardware address pins, so they aren't individually
            // addressable over UART even though addr: exists on the shared base class. If
            // multiple TMC2208 motors share one UART bus, the register values below (and
            // run_amps/hold_amps/microsteps from TrinamicBase) actually applied at runtime
            // are whichever motor is defined LAST in the config file -- earlier motors'
            // values for these same fields are silently overridden. Don't rely on distinct
            // per-motor current/microstepping settings taking effect in a TMC2208 chain.

            // @config run_mode
            // @default StealthChop
            // Chopper algorithm while running: StealthChop (very quiet), CoolStep (runs
            // cooler, allows higher current), or StallGuard (CoolStep plus stall/load
            // detection).
            handler.item("run_mode", _run_mode, trinamicModes);

            // @config homing_mode
            // @default StealthChop
            // Chopper algorithm while homing (same choices as run_mode).
            handler.item("homing_mode", _homing_mode, trinamicModes);

            // @config stallguard
            // @default 0
            // StallGuard sensitivity threshold, -64 (most sensitive) to 63 (least
            // sensitive). Only meaningful when run_mode or homing_mode is StallGuard.
            handler.item("stallguard", _stallguard, -64, 63);

            // @config stallguard_debug
            // @default false
            // Logs live StallGuard sensor values -- useful for tuning the stallguard
            // threshold.
            handler.item("stallguard_debug", _stallguardDebugMode);

            // @config toff_coolstep
            // @default 3
            // TOFF (off-time) register value used in CoolStep/StallGuard mode.
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);
        }

    private:
        TMC2208Stepper* tmc2208 = nullptr;

        bool test();
        void set_registers(bool isHoming);
    };
}
