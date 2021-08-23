#pragma once

/*
    Spindle.h

    Header file for a Spindle Class
    See Spindle.cpp for more details

    Part of Grbl_ESP32

    2020 -	Bart Dring This file was modified for use on the ESP32
                    CPU. Do not use this with Grbl for atMega328P

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

    See SpindleClass.cpp for more info and references

*/

#include <cstdint>

#include "../SpindleDatatypes.h"

#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"

// ===============  No floats! ===========================
// ================ NO FLOATS! ==========================

namespace Spindles {
    class Spindle;
    using SpindleList = std::vector<Spindle*>;

    // This is the base class. Do not use this as your spindle
    class Spindle : public Configuration::Configurable {
    public:
        Spindle() = default;

        Spindle(const Spindle&) = delete;
        Spindle(Spindle&&)      = delete;
        Spindle& operator=(const Spindle&) = delete;
        Spindle& operator=(Spindle&&) = delete;

        bool     _defaultedSpeeds;
        uint32_t offSpeed() { return _speeds[0].offset; }
        uint32_t maxSpeed() { return _speeds[_speeds.size() - 1].speed; }
        uint32_t mapSpeed(SpindleSpeed speed);
        void     setupSpeeds(uint32_t max_dev_speed);
        void     shelfSpeeds(SpindleSpeed min, SpindleSpeed max);
        void     linearSpeeds(SpindleSpeed maxSpeed, float maxPercent);

        static void switchSpindle(uint8_t new_tool, SpindleList spindles, Spindle*& spindle);

        void         spindleDelay(SpindleState state, SpindleSpeed speed);
        virtual void init() = 0;  // not in constructor because this also gets called when $$ settings change

        // Used by Protocol.cpp to restore the state during a restart
        virtual void setState(SpindleState state, uint32_t speed) = 0;
        SpindleState get_state() { return _current_state; };
        void         stop() { setState(SpindleState::Disable, 0); }
        virtual void config_message() = 0;
        virtual bool isRateAdjusted();

        virtual void setSpeedfromISR(uint32_t dev_speed) = 0;

        void spinDown() { setState(SpindleState::Disable, 0); }

        bool                  is_reversable;
        volatile SpindleState _current_state = SpindleState::Unknown;
        volatile SpindleSpeed _current_speed = 0;

        // scaler units are ms/rpm * 2^16.
        // The computation is deltaRPM * scaler >> 16
        uint32_t _spinup_ms   = 0;
        uint32_t _spindown_ms = 0;

        int _tool = -1;

        std::vector<Configuration::speedEntry> _speeds;

        // Name is required for the configuration factory to work.
        virtual const char* name() const = 0;

        // Configuration handlers:
        void validate() const override {
            // TODO: Validate spinup/spindown delay?
        }

        void afterParse() override;

        void group(Configuration::HandlerBase& handler) override {
            handler.item("spinup_ms", _spinup_ms);
            handler.item("spindown_ms", _spindown_ms);
            handler.item("tool", _tool);
            handler.item("speeds", _speeds);
        }

        // Virtual base classes require a virtual destructor.
        virtual ~Spindle() {}
    };
    using SpindleFactory = Configuration::GenericFactory<Spindle>;
}
extern Spindles::Spindle* spindle;
