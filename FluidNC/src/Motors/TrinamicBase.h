// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StandardStepper.h"
#include "../EnumItem.h"

#include <cstdint>

namespace MotorDrivers {

    enum TrinamicMode {
        StealthChop = 0,  // very quiet
        CoolStep    = 1,  // cooler so higher current possible
        StallGuard  = 2,  // coolstep plus stall indication
    };

    extern EnumItem trinamicModes[];

    class TrinamicBase : public StandardStepper {
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
        float _r_sense     = 0.11;
        bool  _use_enable  = false;

        float _run_current         = 0.50;
        float _hold_current        = 0.50;
        int   _microsteps          = 16;
        int   _stallguard          = 0;
        bool  _stallguardDebugMode = false;

        uint8_t _toff_disable     = 0;
        uint8_t _toff_stealthchop = 5;
        uint8_t _toff_coolstep    = 3;

        // Linked list of Trinamic driver instances, used by the
        // StallGuard reporting task.
        static TrinamicBase* List;
        TrinamicBase*        link;
        static void          readSgTask(void*);

        const double fclk = 12700000.0;  // Internal clock Approx (Hz) used to calculate TSTEP from homing rate

        bool report_open_load(bool ola, bool olb);
        bool report_short_to_ground(bool s2ga, bool s2gb);
        bool report_over_temp(bool ot, bool otpw);
        bool report_short_to_ps(bool vsa, bool vsb);

        const char* yn(bool v) { return v ? "Y" : "N"; }

    public:
        TrinamicBase(uint16_t partNumber) : StandardStepper(), _driver_part_number(partNumber) {}

        void group(Configuration::HandlerBase& handler) override {
            handler.item("r_sense_ohms", _r_sense);
            handler.item("run_amps", _run_current);
            handler.item("hold_amps", _hold_current);
            handler.item("microsteps", _microsteps);
            handler.item("stallguard", _stallguard);
            handler.item("stallguard_debug", _stallguardDebugMode);
            handler.item("toff_disable", _toff_disable);
            handler.item("toff_stealthchop", _toff_stealthchop);
            handler.item("toff_coolstep", _toff_coolstep);
            handler.item("run_mode", _run_mode, trinamicModes);
            handler.item("homing_mode", _homing_mode, trinamicModes);
            handler.item("use_enable", _use_enable);

            StandardStepper::group(handler);
        }
    };

}
