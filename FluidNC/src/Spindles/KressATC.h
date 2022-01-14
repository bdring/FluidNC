// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    
        return true for success
        automatic = false for manual tool change    
    bool tool_change(uint8_t new_tool(), bool pre_select)

    void probe_notification()





*/
#include "OnOffSpindle.h"

const int TOOL_COUNT = 4;

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class KressATC : public OnOff {
    public:
        KressATC() = default;

        KressATC(const KressATC&) = delete;
        KressATC(KressATC&&)      = delete;
        KressATC& operator=(const KressATC&) = delete;
        KressATC& operator=(KressATC&&) = delete;

        void atc_init() override;
        bool tool_change(uint8_t new_tool, bool pre_select) override;
        void probe_notification() override;

        void group(Configuration::HandlerBase& handler) override {
            handler.item("atc_valve_pin", _atc_valve_pin);
            handler.item("atc_dustoff_pin", _atc_dustoff_pin);
            handler.item("toolsetter_dustoff_pin", _toolsetter_dustoff);

            OnOff::group(handler);
        }

        ~KressATC() {}

        // Configuration handlers:

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "kress_atc"; }

    protected:
        bool return_tool(uint8_t tool_num);
        void go_above_tool(uint8_t tool_num);
        bool set_ATC_open(bool open);
        bool atc_toolsetter();
        void goto_top_of_z();

        typedef struct {
            float mpos[MAX_N_AXIS];    // the pickup location in machine coords
            float offset[MAX_N_AXIS];  // TLO from the zero'd tool
        } tool_t;

        Pin _atc_valve_pin;
        Pin _atc_dustoff_pin;
        Pin _toolsetter_dustoff;

        const int   ETS_INDEX        = 0;     // electronic tool setter index
        const float TOOL_GRAB_TIME   = 0.25;  // seconds. How long it takes to grab a tool
        const float RACK_SAFE_DIST_Y = 25.0;  // how far in front of rack is safe to move in X
        const float ATC_EMPTY_SAFE_Z = 135.0;   // at what Z in mpos can an empty atc traverse the rack with no tool
        const float PROBE_FEEDRATE   = 600.0;

        int zeroed_tool_index = 1;  // Which tool was zero'd on the work piece

        float top_of_z            = 0.0;    // position of top of Z in mpos, for safe XY travel
        bool  tool_setter_probing = false;  // used to determine if current probe cycle is for the setter

        //float tool_location[TOOL_COUNT][MAX_N_AXIS];

        tool_t tool[TOOL_COUNT + 1];  // 0 is the toolsetter
    };
}
