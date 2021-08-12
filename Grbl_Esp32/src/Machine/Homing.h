#pragma once

/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn, Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../Configuration/Configurable.h"
#include "../System.h"  // AxisMask, MotorMask

namespace Machine {
    class Homing : public Configuration::Configurable {
    // The return value is the setting time
    static uint32_t  plan_move(MotorMask motors, bool approach, bool seek, float customPulloff);
    static void      run(MotorMask remainingMotors, bool approach, bool seek, float customPulloff);
    static bool      squaredOneSwitch(MotorMask motors);
    static bool      squaredStressfree(MotorMask motors);
    static void      set_mpos(AxisMask axisMask);
    static void      run_one_cycle(AxisMask axisMask);
    static const int REPORT_LINE_NUMBER = 0;

public:
    Homing() = default;

    static const int AllCycles = 0;  // Must be zero.

    static void run_cycles(AxisMask axisMask);

    // The homing cycles are 1,2,3 etc.  0 means not homed as part of home-all,
    // but you can still home it manually with e.g. $HA
    int      _cycle             = -1;  // what auto-homing cycle does this axis home on?
    bool     _positiveDirection = true;
    float    _mpos              = 0.0f;    // After homing this will be the mpos of the switch location
    float    _feedRate          = 50.0f;   // pulloff and second touch speed
    float    _seekRate          = 200.0f;  // this first approach speed
    uint32_t _debounce_ms       = 250;     // ms settling time for homing switches after motion
    float    _seek_scaler       = 1.1f;    // multiplied by max travel for max homing distance on first touch
    float    _feed_scaler       = 1.1f;    // multiplier to pulloff for moving to switch after pulloff

    // Configuration system helpers:
    void validate() const override { Assert(_cycle >= 0, "Homing cycle must be defined"); }

    void group(Configuration::HandlerBase& handler) override {
        handler.item("cycle", _cycle);
        handler.item("positive_direction", _positiveDirection);
        handler.item("mpos", _mpos);
        handler.item("feed_rate", _feedRate);
        handler.item("seek_rate", _seekRate);
        handler.item("debounce_ms", _debounce_ms);
        handler.item("seek_scaler", _seek_scaler);
        handler.item("feed_scaler", _feed_scaler);
        }

        void init() {}
    };
}
