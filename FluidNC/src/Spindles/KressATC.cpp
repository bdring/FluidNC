// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    TO DO

    Turn off soft limits during tool changes. This would
    allow the rack to be placed outside of soft limit zone
    This would prevent user from damaging the rack

    We may consider handling delays here to speed up tool changes.
    Spindle could be turned off and moved to rack location while spinning down
    Delay would be added as required if the time has not expired before getting there.

    Calc top_of_z from limits

    Need to fail and quite if no probe defined at the time of probing

*/

#include "KressATC.h"
#include "../Protocol.h"
#include "../GCode.h"
#include "../Uart.h"

// ========================= KressATC ==================================

namespace Spindles {
    void KressATC::atc_init() {
        _atc_valve_pin.setAttr(Pin::Attr::Output);
        _atc_dustoff_pin.setAttr(Pin::Attr::Output);
        _toolsetter_dustoff.setAttr(Pin::Attr::Output);

        log_info("ATC Init Valve:" << _atc_valve_pin.name() << " Dustoff:" << _atc_dustoff_pin.name());
        top_of_z = 147.00;  // in machine position

        // the tool setter
        tool[ETS_INDEX].mpos[X_AXIS] = 157;
        tool[ETS_INDEX].mpos[Y_AXIS] = 142;
        tool[ETS_INDEX].mpos[Z_AXIS] = 119.0;  // Mpos before collet face triggers probe

        tool[1].mpos[X_AXIS] = 197.0;
        tool[1].mpos[Y_AXIS] = 142.0;
        tool[1].mpos[Z_AXIS] = 130.00;

        tool[2].mpos[X_AXIS] = 237.0;
        tool[2].mpos[Y_AXIS] = 142.0;
        tool[2].mpos[Z_AXIS] = 130.0;

        tool[3].mpos[X_AXIS] = 277.0;
        tool[3].mpos[Y_AXIS] = 142.0;
        tool[3].mpos[Z_AXIS] = 130.0;

        tool[4].mpos[X_AXIS] = 317.0;
        tool[4].mpos[Y_AXIS] = 142.0;
        tool[4].mpos[Z_AXIS] = 130.0;
    }

    bool KressATC::tool_change(uint8_t new_tool, bool pre_select) {
        bool  spindle_was_on         = false;
        bool  was_incremental_mode   = false;  // started in G91 mode
        float saved_mpos[MAX_N_AXIS] = {};     // the position before the tool change

        if (new_tool == current_tool) {
            return true;
        }

        protocol_buffer_synchronize();  // wait for all previous moves to complete
        motor_steps_to_mpos(saved_mpos, motor_steps);

        // see if we need to switch out of incremental (G91) mode
        if (gc_state.modal.distance == Distance::Incremental) {
            gc_exec_linef(false, Uart0, "G90");
            was_incremental_mode = true;
        }

        // is spindle on? Turn it off and determine when the spin down should be done.
        if (gc_state.modal.spindle != SpindleState::Disable) {
            spindle_was_on = true;
            gc_exec_linef(true, Uart0, "M5");  // this should add a delay if there is one
            if (spindle->_spindown_ms == 0) {
                vTaskDelay(10000);  // long delay for safety and to prevent ATC damage
            }
        }

        // ============= Start of tool change ====================

        goto_top_of_z();

        // return the current tool if there is one.
        if (!return_tool(current_tool)) {  // does nothing if we have no tool
            gc_exec_linef(true, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[new_tool].mpos[X_AXIS], tool[new_tool].mpos[Y_AXIS]);
        }

        current_tool = 0;  // now we have no tool

        if (new_tool == 0) {  // if changing to tool 0...we are done.
            gc_exec_linef(true, Uart0, "G53 G0 Y%0.3f", tool[0].mpos[Y_AXIS] - RACK_SAFE_DIST_Y);
            current_tool = new_tool;
            return true;
        }

        go_above_tool(new_tool);

        set_ATC_open(true);                                                        // open ATC
        gc_exec_linef(true, Uart0, "G53 G0 Z%0.3f", tool[new_tool].mpos[Z_AXIS]);  // drop down to tool
        set_ATC_open(false);                                                       // Close ATC
        gc_exec_linef(true, Uart0, "G4 P%0.2f", TOOL_GRAB_TIME);                   // wait for grab to complete and settle
        goto_top_of_z();

        current_tool = new_tool;

        if (!atc_toolsetter()) {  // check the length of the tool
            return false;
        }

        // ================== return old states ===================

        // If the spindle was on before we started, we need to turn it back on.
        if (spindle_was_on) {
            gc_exec_linef(false, Uart0, "M3");  // spindle should handle spinup delay
        }

        // return to saved mpos in XY
        gc_exec_linef(false, Uart0, "G53 G0 X%0.3f Y%0.3f Z%0.3f", saved_mpos[X_AXIS], saved_mpos[Y_AXIS], top_of_z);

        // return to saved mpos in Z if it is not outside of work area.
        gc_exec_linef(false, Uart0, "G53 G0 Z%0.3f", saved_mpos[Z_AXIS]);

        // was was_incremental on? If so, return to that state
        if (was_incremental_mode) {
            gc_exec_linef(false, Uart0, "G91");
        }

        return true;
    }

    bool KressATC::return_tool(uint8_t tool_num) {
        if (tool_num == 0) {
            return false;
        }

        go_above_tool(tool_num);
        gc_exec_linef(true, Uart0, "G53 G0 Z%0.3f", tool[tool_num].mpos[Z_AXIS]);  // drop down to tool
        set_ATC_open(true);
        goto_top_of_z();
        set_ATC_open(false);  // close ATC

        return true;
    }

    void KressATC::go_above_tool(uint8_t tool_num) {
        goto_top_of_z();

        if (current_tool != 0) {
            // move in front of tool
            gc_exec_linef(false, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[tool_num].mpos[X_AXIS], tool[tool_num].mpos[Y_AXIS] - RACK_SAFE_DIST_Y);
        }
        // Move over tool
        gc_exec_linef(true, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[tool_num].mpos[X_AXIS], tool[tool_num].mpos[Y_AXIS]);
    }

    bool KressATC::set_ATC_open(bool open) {
        if (gc_state.modal.spindle != SpindleState::Disable) {
            return false;
        }
        _atc_valve_pin.synchronousWrite(open);
        return true;
    }

    bool KressATC::atc_toolsetter() {
        float probe_to;  // Calculated work position
        float probe_position[MAX_N_AXIS];

        if (current_tool == 1) {
            // we can go straight to the ATC because tool 1 is next to the toolsetter
            gc_exec_linef(true, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[ETS_INDEX].mpos[X_AXIS], tool[ETS_INDEX].mpos[Y_AXIS]);  // Move over tool
        } else {
            gc_exec_linef(false, Uart0, "G91");
            // Arc out of current tool
            gc_exec_linef(false, Uart0, "G2 X-%0.3f Y-%0.3f I-%0.3f F4000", RACK_SAFE_DIST_Y, RACK_SAFE_DIST_Y, RACK_SAFE_DIST_Y);

            // Move it to arc start
            gc_exec_linef(false,
                          Uart0,
                          "G53 G0X%0.3f Y%0.3f",
                          tool[ETS_INDEX].mpos[X_AXIS] + RACK_SAFE_DIST_Y,
                          tool[ETS_INDEX].mpos[Y_AXIS] - RACK_SAFE_DIST_Y);

            // arc in
            gc_exec_linef(false, Uart0, "G2 X-%0.3f Y%0.3f J%0.3f F4000", RACK_SAFE_DIST_Y, RACK_SAFE_DIST_Y, RACK_SAFE_DIST_Y);
            gc_exec_linef(false, Uart0, "G90");
            // Move over tool
            gc_exec_linef(true, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[ETS_INDEX].mpos[X_AXIS], tool[ETS_INDEX].mpos[Y_AXIS]);
        }

        //atc_ETS_dustoff();

        float wco = gc_state.coord_system[Z_AXIS] + gc_state.coord_offset[Z_AXIS] + gc_state.tool_length_offset;
        probe_to  = tool[ETS_INDEX].mpos[Z_AXIS] - wco;

        // https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G38-probe
        tool_setter_probing = true;
        gc_exec_linef(true, Uart0, "G38.2 F%0.3f Z%0.3f", PROBE_FEEDRATE, probe_to);  // probe
        tool_setter_probing = false;

        // Was probe successful?
        if (sys.state == State::Alarm) {
            if (rtAlarm == ExecAlarm::ProbeFailInitial) {
                log_info("ATC Probe Switch Error");
            } else {
                log_info("ATC Missing Tool");
            }
            return false;  // fail
        }

        motor_steps_to_mpos(probe_position, probe_steps);
        tool[current_tool].offset[Z_AXIS] = probe_position[Z_AXIS];  // Get the Z height ...

        if (zeroed_tool_index != 0) {
            float tlo = tool[current_tool].offset[Z_AXIS] - tool[zeroed_tool_index].offset[Z_AXIS];
            log_info("ATC Tool No:" << current_tool << " TLO:" << tlo);
            // https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G43_1
            gc_exec_linef(false, Uart0, "G43.1 Z%0.3f", tlo);  // raise up
        }

        goto_top_of_z();
        // move forward
        gc_exec_linef(false, Uart0, "G53 G0 X%0.3f Y%0.3f", tool[ETS_INDEX].mpos[X_AXIS], tool[ETS_INDEX].mpos[Y_AXIS] - RACK_SAFE_DIST_Y);

        return true;
    }

    void KressATC::goto_top_of_z() {
        gc_exec_linef(false, Uart0, "G53 G0 Z%0.3f", top_of_z);  // Go to top of Z travel
    }

    void KressATC::probe_notification() {
        float probe_position[MAX_N_AXIS];

        if (sys.state == State::Alarm) {
            return;  // probe failed
        }

        if (tool_setter_probing) {
            return;  // ignore these probes. They are handled elsewhere.
        }

        zeroed_tool_index = current_tool;
    }

    // Configuration registration    namespace {
    SpindleFactory::InstanceBuilder<KressATC> registration("kress_atc");
}
}
