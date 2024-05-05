// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StandardStepper.h"
#include "../EnumItem.h"
#include <TMCStepper.h>  // https://github.com/teemuatlut/TMCStepper
#include <cstdint>

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
        uint32_t calc_tstep(float speed, float percent);

        bool         _disable_state_known = false;  // we need to always set the state least once.
        bool         _has_errors;
        uint16_t     _driver_part_number;  // example: use 2130 for TMC2130
        bool         _disabled = false;
        TrinamicMode _mode     = TrinamicMode::StealthChop;

        // Configurable
        int   _homing_mode = StealthChop;
        int   _run_mode    = StealthChop;
        float _r_sense     = 0;
        bool  _use_enable  = false;

        float _run_current         = 0.50;
        float _hold_current        = 0.50;
        float _homing_current      = 0.50;
        int   _microsteps          = 16;
        int   _stallguard          = 0;
        bool  _stallguardDebugMode = false;

        uint8_t _toff_disable     = 0;
        uint8_t _toff_stealthchop = 5;
        uint8_t _toff_coolstep    = 3;

        static constexpr double fclk = 12700000.0;  // Internal clock Approx (Hz) used to calculate TSTEP from homing rate

        float        holdPercent();
        bool         report_open_load(bool ola, bool olb);
        bool         report_short_to_ground(bool s2ga, bool s2gb);
        bool         report_over_temp(bool ot, bool otpw);
        bool         report_short_to_ps(bool vsa, bool vsb);
        bool         set_homing_mode(bool isHoming);
        virtual void set_registers(bool isHoming) {}
        bool         reportTest(uint8_t result);
        void         reportCommsFailure(void);
        bool         checkVersion(uint8_t expected, uint8_t got);
        bool         startDisable(bool disable);
        virtual void config_motor();

        const char* yn(bool v) { return v ? "Y" : "N"; }

        void registration();

    public:
        TrinamicBase() = default;

        void group(Configuration::HandlerBase& handler) override {
            StandardStepper::group(handler);

            handler.item("r_sense_ohms", _r_sense, 0.0, 1.00);
            handler.item("run_amps", _run_current, 0.05, 10.0);
            handler.item("hold_amps", _hold_current, 0.05, 10.0);
            handler.item("microsteps", _microsteps, 1, 256);
            handler.item("toff_disable", _toff_disable, 0, 15);
            handler.item("toff_stealthchop", _toff_stealthchop, 2, 15);
            handler.item("use_enable", _use_enable);
        }
    };

}
