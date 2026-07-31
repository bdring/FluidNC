// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

#include "SpindleDatatypes.h"
#include "Machine/Macros.h"
#include "Configuration/Configurable.h"
#include "Configuration/GenericFactory.h"
#include "GCode.h"  // MaxToolNumber
#include "Module.h"
#include "ToolChangers/atc.h"

// ===============  No floats! ===========================
// ================ NO FLOATS! ==========================

namespace Spindles {
    class Spindle;
    using SpindleList = std::vector<Spindle*>;

    // This is the base class. Do not use this as your spindle
    class Spindle : public Configuration::Configurable {
    private:
        const char* _name;
        std::string _atc_info = "";

        // _zero_speed_with_disable forces speed to 0 when disabled
        bool _zero_speed_with_disable = false;

    protected:
        ATCs::ATC* _atc       = nullptr;
        uint32_t   _last_tool = 0;

    public:
        // _disable_with_zero_speed forces a disable when speed is 0
        bool _disable_with_zero_speed = false;

        Spindle(const char* name) : _name(name) {}

        Spindle(const Spindle&)            = delete;
        Spindle(Spindle&&)                 = delete;
        Spindle& operator=(const Spindle&) = delete;
        Spindle& operator=(Spindle&&)      = delete;

        bool     _defaultedSpeeds;
        uint32_t offSpeed() { return _speeds[0].offset; }
        uint32_t maxSpeed();
        uint32_t mapSpeed(SpindleState state, SpindleSpeed speed);
        void     setupSpeeds(uint32_t max_dev_speed);
        void     shelfSpeeds(SpindleSpeed min, SpindleSpeed max);
        void     linearSpeeds(SpindleSpeed maxSpeed, float maxPercent);

        static void switchSpindle(uint32_t new_tool, SpindleList spindles, Spindle*& spindle, bool& stop_spindle, bool& new_spindle);

        void         spindleDelay(SpindleState state, SpindleSpeed speed);
        virtual void init() = 0;  // not in constructor because this also gets called when $$ settings change
        virtual void init_atc();
        std::string  atc_info() { return _atc_info; };

        // Used by Protocol.cpp to restore the state during a restart
        virtual void   setState(SpindleState state, uint32_t speed) = 0;
        SpindleState   get_state() { return _current_state; };
        void           stop() { setState(SpindleState::Disable, 0); }
        virtual void   config_message() = 0;
        virtual bool   isRateAdjusted();
        virtual tool_t get_current_tool_num() { return _current_tool; }
        virtual bool   tool_change(uint32_t tool_number, bool pre_select, bool set_tool);

        virtual void setSpeedfromISR(uint32_t dev_speed) = 0;

        void spinDown() { setState(SpindleState::Disable, 0); }

        bool                  is_reversable;
        volatile SpindleState _current_state = SpindleState::Unknown;
        volatile SpindleSpeed _current_speed = 0;

        // scaler units are ms/rpm * 2^16.
        // The computation is deltaRPM * scaler >> 16
        uint32_t _spinup_ms   = 0;
        uint32_t _spindown_ms = 0;

        int32_t _tool = 0;

        std::vector<Configuration::speedEntry> _speeds;

        bool _off_on_alarm = false;

        Macro       _m6_macro;
        std::string _atc_name = "";

        // Name is required for the configuration factory to work.
        const char* name() { return _name; }

        // Configuration handlers:
        void afterParse() override;

        void group(Configuration::HandlerBase& handler) override {
            // Shared field set every spindle type (PWM, Laser, 0-10V, ModbusVFD, etc.)
            // inherits via Spindle::group() -- annotated once here, not repeated per type.
            // spinup_ms/spindown_ms are NOT here -- see groupDelaySettings() below.

            // @config tool_num
            // @default 0
            // Sets the tool-number range this spindle responds to for M6 Tn tool changes.
            // With a single spindle, the value doesn't matter (conventionally 0). With
            // multiple spindles, give each a distinct number -- ranges are implied by
            // relative order, e.g. a relay spindle at tool_num: 0 and a laser at
            // tool_num: 100 makes M6 T0-T99 select the relay and M6 T100+ select the laser.
            handler.item("tool_num", _tool, 0, MaxToolNumber);

            // @config speed_map
            // @default "" (empty)
            // Maps GCode S values to actual spindle speeds/PWM duty -- lets the S-to-speed
            // relationship be linearized or clamped to a minimum speed. See the speed-map
            // documentation for the full syntax.
            handler.item("speed_map", _speeds);

            // @config off_on_alarm
            // @default false
            // Turns the spindle off whenever an alarm occurs. Worth enabling with a safety
            // door in use, since the parking feature doesn't operate while in alarm state.
            handler.item("off_on_alarm", _off_on_alarm);

            // @config atc
            // @default "" (empty)
            // Names an atc_manual:/ATC section (defined elsewhere in the config) to
            // associate with this spindle for automatic tool changes.
            handler.item("atc", _atc_name);

            // @config m6_macro
            // @default "" (empty)
            // A macro (one config-file line, same syntax as macros:) to run for this
            // spindle's M6 tool change, instead of the built-in tool-change behavior.
            handler.item("m6_macro", _m6_macro);

            // @config s0_with_disable
            // @default false
            // When true, an M5 (spindle off) also forces the speed signal to S0 (zero) --
            // by default the speed output stays at its last commanded value even during M5.
            handler.item("s0_with_disable", _zero_speed_with_disable);

            // @config disable_with_s0
            // @default false
            // When true, commanding S0 (zero speed) also disables the spindle, the same as
            // M5 -- by default, only M5 itself disables it.
            handler.item("disable_with_s0", _disable_with_zero_speed);
        }

        // spinup_ms/spindown_ms only make sense for spindle types with a mechanical
        // ramp-up/down to wait out. Not called from group() itself -- subclasses that
        // want these fields call this explicitly (OnOff::group(), HBridge::group(),
        // VFDSpindle::group()); Laser/PlasmaSpindle simply never call it.
        void groupDelaySettings(Configuration::HandlerBase& handler) {
            // @config spinup_ms
            // @default 0
            // Time given for the spindle to reach the commanded RPM (per the speed
            // map) before the following GCode line executes. Proportional to the RPM
            // change -- a half-scale speed change only waits half of this value.
            handler.item("spinup_ms", _spinup_ms, 0, 60000);

            // @config spindown_ms
            // @default 0
            // Same as spinup_ms, but applied when the commanded RPM decreases.
            handler.item("spindown_ms", _spindown_ms, 0, 60000);
        }

        // Virtual base classes require a virtual destructor.
        virtual ~Spindle() {}

    protected:
        tool_t _current_tool = 0;
    };

    using SpindleFactory = Configuration::GenericFactory<Spindle>;
}
extern Spindles::Spindle* spindle;
