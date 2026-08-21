// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StandardStepper.h"
#include "EnumItem.h"
#include <cstdint>       // Must be before TMCStepper.h
#include <TMCStepper.h>  // https://github.com/teemuatlut/TMCStepper

namespace MotorDrivers {

    enum TrinamicMode {
        StealthChop = 0,  // very quiet
        CoolStep    = 1,  // cooler so higher current possible
        StallGuard  = 2,  // coolstep plus stall indication
    };

    extern const EnumItem trinamicModes[];

    class TrinamicBase : public StandardStepper {
    private:
        static void read_sg(TimerHandle_t);

        static std::vector<TrinamicBase*> _instances;

    protected:
        uint32_t calc_tstep(int percent);

        bool         _disable_state_known = false;  // we need to always set the state least once.
        bool         _has_errors;
        uint16_t     _driver_part_number;  // example: use 2130 for TMC2130
        bool         _disabled = false;
        TrinamicMode _mode     = TrinamicMode::StealthChop;

        // Configurable
        uint32_t _homing_mode = StealthChop;
        uint32_t _run_mode    = StealthChop;
        float    _r_sense     = 0;
        bool     _use_enable  = false;

        float   _run_current         = 0.50;
        float   _hold_current        = 0.50;
        float   _homing_current      = 0.0;
        int32_t _microsteps          = 16;
        int32_t _stallguard          = 0;
        bool    _stallguardDebugMode = false;

        uint8_t _toff_disable     = 0;
        uint8_t _toff_stealthchop = 5;
        uint8_t _toff_coolstep    = 3;

        static constexpr double fclk = 12700000.0;  // Internal clock Approx (Hz) used to calculate TSTEP from homing rate

        float        holdPercent();
        bool         report_open_load(bool ola, bool olb);
        bool         report_short_to_ground(bool s2ga, bool s2gb);
        bool         report_over_temp(bool ot, bool otpw);
        bool         report_short_to_ps(bool vsa, bool vsb);
        bool         set_homing_mode(bool isHoming) override;
        virtual void set_registers(bool isHoming) {}
        bool         reportTest(uint8_t result);
        void         reportCommsFailure(void);
        bool         checkVersion(uint8_t expected, uint8_t got);
        bool         startDisable(bool disable);
        void         init() override;
        virtual void config_motor();

        const char* yn(bool v) { return v ? "Y" : "N"; }

        void registration();

    public:
        TrinamicBase(const char* name) : StandardStepper(name) {}

        void group(Configuration::HandlerBase& handler) override {
            // Shared field set for every Trinamic driver (SPI or UART), inherited via
            // TrinamicBase::group() -- annotated once here, not repeated per driver type.

            StandardStepper::group(handler);

            // @config r_sense_ohms
            // @default 0.0
            // Sense resistor value for the physical driver module, in ohms. The 0.0 default
            // is not a real, functional value -- TrinamicBase itself has no correct generic
            // default, since this is purely a property of the specific module in hand
            // (e.g. 0.11 is typical for genuine TMC2130/TMC2208/TMC2209 modules, 0.075 for
            // TMC5160). A real value appropriate to the actual hardware must always be set
            // explicitly.
            handler.item("r_sense_ohms", _r_sense, 0.0, 1.00);

            // @config run_amps
            // @default 0.5
            // Motor current while running, in amps RMS.
            handler.item("run_amps", _run_current, 0.05, 10.0);

            // @config hold_amps
            // @default 0.5
            // Motor current while holding position (not moving), in amps RMS.
            handler.item("hold_amps", _hold_current, 0.05, 10.0);

            // @config microsteps
            // @default 16
            // Microstep resolution. Needs to be reflected in the axis's steps_per_mm.
            handler.item("microsteps", _microsteps, 1, 256);

            // @config toff_disable
            // @default 0
            // TOFF (off-time) register value used while the driver is disabled.
            handler.item("toff_disable", _toff_disable, 0, 15);

            // @config toff_stealthchop
            // @default 5
            // TOFF (off-time) register value used in StealthChop mode.
            handler.item("toff_stealthchop", _toff_stealthchop, 2, 15);

            // @config use_enable
            // @default false
            // Uses disable_pin as an active enable signal (inverted sense) instead of the
            // ordinary active-disable sense -- some driver modules wire this pin the
            // opposite way from the FluidNC default.
            handler.item("use_enable", _use_enable);
        }
    };
}
