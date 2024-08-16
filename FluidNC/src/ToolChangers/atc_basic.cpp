// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "atc_basic.h"
#include "../Machine/MachineConfig.h"
#include <cstdio>
#include <iostream>

namespace ATCs {
    void Basic_ATC::init() {
        log_info("ATC:" << name());
    }

    void Basic_ATC::probe_notification() {}

    bool Basic_ATC::tool_change(uint8_t new_tool, bool pre_select, bool set_tool) {
        protocol_buffer_synchronize();  // wait for all motion to complete
        _macro.erase();             // clear previous gcode
        
        if (pre_select) { // not implemented
            return true;
        }

        // set_tool is used to update the current tool and reset the TLO to 0
        if (set_tool) {
            _prev_tool = new_tool;
            _macro.addf("G4P0 0.0");
            if (!_have_tool_setter_offset) {
                get_ets_offset();
            }
            _macro.run(nullptr);
            return true;
        }
        
        //save machine states
        bool spindle_was_on = (gc_state.modal.spindle != SpindleState::Disable);  // used to restore the spindle state
        bool was_inch_mode  = (gc_state.modal.units == Units::Inches);
        bool was_absolute_mode = (gc_state.modal.distance == Distance::Absolute);
        bool mistcoolant_was_on= (gc_state.modal.coolant.Mist);
        bool floodcoolant_was_on = (gc_state.modal.coolant.Flood);
        // save current location, so we can return after the tool change.
        _macro.addf("#<start_x >= #<_x>");
        _macro.addf("#<start_y >= #<_y>");
        _macro.addf("#<start_z >= #<_z>");
        
        if (mistcoolant_was_on || floodcoolant_was_on) {
            _macro.addf("M9");
        }
        if (was_inch_mode) { // become Metric
            _macro.addf("G21");
        }
        if (!was_absolute_mode) { // become absolute
            _macro.addf("G90");
        }
        if (spindle_was_on) { // turn off the spindle
            _macro.addf("M5");
        }

        try {
            if (_prev_tool > 0) {
                // return tool
                move_to_tool_position(_prev_tool);
                _macro.addf(_toolreturn_macro._gcode.c_str()); // use macro with G91 movements or the _tc_tool_* variables to to return tool, operating the ATC using M62 & M63
                // ensure the macro didn't change positioning mode
                _macro.addf("G90");
                _macro.addf("G21");
            }

            if (new_tool > 0) {
                //pickup tool
                move_to_tool_position(_prev_tool);
                _macro.addf(_toolpickup_macro._gcode.c_str()); // use macro with G91 movements or the _tc_tool_* variables to to pickup tool, operating the ATC using M62 & M63
                // ensure the macro didn't change positioning mode
                _macro.addf("G90");
                _macro.addf("G21");
                if (!_have_tool_setter_offset) {
                    get_ets_offset();
                }
            }

            // probe the new tool
            ets_probe();

            // TLO is simply the difference between the tool1 probe and the new tool probe.
            _macro.addf("#<_my_tlo_z >=[#5063 - #<_ets_tool1_z>]");
            _macro.addf("G43.1Z#<_my_tlo_z>");

            // return to location before the tool change
            move_to_safe_z();
            _macro.addf("G0 X#<start_x>Y#<start_y>");
            _macro.addf("G0 Z#<start_z>");
            if (was_inch_mode) {
                _macro.addf("G20");
            }
            if (!was_absolute_mode) { // become relative
                _macro.addf("G91");
            }
            if (spindle_was_on) {
                _macro.addf("M3");  // spindle should handle spinup delay
            }
            if (mistcoolant_was_on) {
                _macro.addf("M7");
            }
            if (floodcoolant_was_on) {
                _macro.addf("M8");
            }
            _macro.run(nullptr);

            return true;
        } catch (...) { log_info("Exception caught"); }

        return false;
    }

    void Basic_ATC::move_to_tool_position(uint8_t tool_index) {
        tool_index -= 1;
        move_to_safe_z();
        _macro.addf("G53 G0 X%0.3f Y%0.3f", _tool_mpos[tool_index][X_AXIS], _tool_mpos[tool_index][Y_AXIS]);
        _macro.addf("#<_tc_tool_x >=%0.3f",_tool_mpos[tool_index][X_AXIS]);
        _macro.addf("#<_tc_tool_y >=%0.3f",_tool_mpos[tool_index][Y_AXIS]);
        _macro.addf("#<_tc_tool_z >=%0.3f",_tool_mpos[tool_index][Z_AXIS]);
    }

    void Basic_ATC::move_to_safe_z() {
        _macro.addf("G53 G0 Z%0.3f", _safe_z);
    }

    void Basic_ATC::move_over_toolsetter() {
        move_to_safe_z();
        _macro.addf("G53 G0 X%0.3fY%0.3f", _ets_mpos[0], _ets_mpos[1]);
    }

    void Basic_ATC::get_ets_offset(){
        ets_probe();
        _macro.addf("#<_ets_tool1_z>=[#5063]");  // save the value of the tool1 ETS Z
        _have_tool_setter_offset = true;
    }

    void Basic_ATC::ets_probe() {
        move_to_safe_z();
        move_over_toolsetter();
        _macro.addf("G53 G0 Z #</ atc_manual / ets_rapid_z_mpos_mm>");  // rapid down

        // do a fast probe if there is a seek that is faster than feed
        if (_probe_seek_rate > _probe_feed_rate) {
            _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_seek_rate);
            _macro.addf("G0 Z[#<_z> + 5]");  // retract before next probe
        }
        // do the feed rate probe
        _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_feed_rate);
    }

    namespace {
        ATCFactory::InstanceBuilder<Basic_ATC> registration("atc_basic");
    }
}
