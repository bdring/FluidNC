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

        static constexpr int32_t UNSET_STALLGUARD_SEEK = INT32_MIN;

        float   _run_current         = 0.50;
        float   _hold_current        = 0.50;
        float   _homing_current      = 0.0;
        int32_t _microsteps          = 16;
        int32_t _stallguard          = 0;
        int32_t _stallguard_seek     = UNSET_STALLGUARD_SEEK;  // defaults to _stallguard in afterParse()
        bool    _stallguardDebugMode = false;
        bool    _fastHomingPhase     = false;  // true during the homing seek (fast approach) phase

        // The stallguard threshold for the current homing phase: the seek
        // (fast approach) phase can use a different sensitivity than the
        // feed (slow approach) phase.
        int32_t active_stallguard() const { return _fastHomingPhase ? _stallguard_seek : _stallguard; }

        // Called when a homing cycle transitions between the seek and feed
        // phases; drivers with phase-dependent registers override
        // apply_homing_phase() to reprogram them.
        virtual void apply_homing_phase() {}

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

        void set_homing_phase(bool fastApproach) override {
            _fastHomingPhase = fastApproach;
            if (_mode == TrinamicMode::StallGuard) {
                apply_homing_phase();
            }
        }

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
