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
const int MANUAL_CHG = TOOL_COUNT + 1;

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
        bool is_ATC_ok();
        void deactivate() override;

        void group(Configuration::HandlerBase& handler) override {
            handler.item("atc_valve_pin", _atc_valve_pin);
            handler.item("atc_dustoff_pin", _atc_dustoff_pin);
            handler.item("ets_dustoff_pin", _toolsetter_dustoff);
            handler.item("ets_mpos_mm", _ets_mpos);
            handler.item("tool1_mpos_mm", _tool_mpos[0]);
            handler.item("tool2_mpos_mm", _tool_mpos[1]);
            handler.item("tool3_mpos_mm", _tool_mpos[2]);
            handler.item("tool4_mpos_mm", _tool_mpos[3]);
            handler.item("empty_safe_z", _empty_safe_z);

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

        
        const int     ETS_INDEX        = 0;      // electronic tool setter index
        const float   TOOL_GRAB_TIME   = 0.25;   // seconds. How long it takes to grab a tool
        const float   RACK_SAFE_DIST_Y = 25.0;   // how far in front of rack is safe to move in X
        const float   PROBE_FEEDRATE   = 300.0;

        Pin                _atc_valve_pin;
        Pin                _atc_dustoff_pin;
        Pin                _toolsetter_dustoff;
        std::vector<float> _ets_mpos;
        std::vector<float> _tool_mpos[TOOL_COUNT];


        int zeroed_tool_index = 1;  // Which tool was zero'd on the work piece
        bool  _atc_ok              = false;
        float top_of_z            = -1.0;    // position of top of Z in mpos, for safe XY travel
        bool  tool_setter_probing = false;  // used to determine if current probe cycle is for the setter
        float _empty_safe_z   = 0;  // machine space location where is it safe to cross over tools when empty

        //float tool_location[TOOL_COUNT][MAX_N_AXIS];

        tool_t tool[TOOL_COUNT + 1];  // 0 is the toolsetter
    };
}
