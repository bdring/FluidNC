// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "atc_manual.h"
#include "../Machine/MachineConfig.h"

/*
  safe_z_mm: Set this to the mpos height you want the Z to travel around when tool changing. It is typically near the top so the longest tool can clear the work.
  change_mpos_mm: This is where the machine will go for the manual tool change. 

  ets_mpos_mm: The X and Y location are the XY center of the toolsetter. The Z is the lowest the Z should go before we fail due to missing bit.

  How do you tell FNC you already have a tool number installed before starting a job.

  How do you tell it you want to install a tool before a job.

  M6T0 from T<anything> -- Resets the offsets for a new job

  M6T<not 0> From T0 --  Moves to the change location and does nothing else. Assumes work zero needs to be set
  
  M6T<not 0> to T<anything> first time 
     -- Determines the TS offset
     -- Goes to toolchange location
     -- Set TLO
     -- Returns to position before command

  M6T<not 0> to T<anything> after first time
     -- Goes to toolchange location
     -- Set TLO
     -- Returns to position before command

  Posible New Persistant values (might want a save_ATC_values: config item. default false)
     -- TLO
     -- Tool number


tool_changer:
  safe_z_mpos_mm: -1.000000
  probe_seek_rate_mm_per_min: 800.000000
  probe_feed_rate_mm_per_min: 80.000000
  change_mpos_mm: 80.000 0.000 -1.000
  ets_mpos_mm: 5.000 -17.000 -40.000
      



*/

namespace ATCs {
    void Manual_ATC::init() {
        log_info("ATC:" << name());
    }

    void Manual_ATC::probe_notification() {}

    bool Manual_ATC::tool_change(uint8_t new_tool, bool pre_select, bool set_tool) {
        bool spindle_was_on = false;

        if (new_tool == 0) {  // M6T0 is used to reset this ATC
            log_info("ATC values reset");
            reset();
            move_to_change_location();
            return true;
        }

        if (new_tool == _prev_tool) {
            log_info("Requested tool already active");
            return true;
        }

        try {
            protocol_buffer_synchronize();  // wait for all motion to complete

            if (_prev_tool == 0) {  // M6T<anything> from T0 is used for a manual change before zero'ing
                log_info("Load first tool");
                move_to_change_location();
                _prev_tool = new_tool;
                return true;
            }

            float  starting_wpos[6] = {};
            float* mpos             = get_mpos();
            float* wco              = get_wco();

            auto n_axis = config->_axes->_numberAxis;
            for (int idx = 0; idx < n_axis; idx++) {
                starting_wpos[idx] = mpos[idx] - wco[idx];
            }
            //log_info("Starting WPos: " << starting_wpos[0] << "," << starting_wpos[1] << "," << starting_wpos[2]);

            _prev_tool = new_tool;

            move_to_save_z();
            if (gc_state.modal.spindle != SpindleState::Disable) {
                spindle_was_on = true;
                gc_exec_linef(true, Uart0, "M5");  // this should add a delay if there is one
            }

            // if we have not determined the tool setter offset yet, we need to do that.
            if (!_have_tool_setter_offset) {
                move_over_toolsetter();
                // do a seek probe if needed
                if (!seek_probe())
                    return false;
                // do the position finding feed rate probe
                if (!probe(_probe_feed_rate, _tool_setter_position)) {
                    return false;
                }

                _tool_setter_offset      = _tool_setter_position[2];
                _have_tool_setter_offset = true;
            }

            move_to_change_location();
            log_info("Please install tool:" << new_tool);

            if (!hold_and_wait_for_resume()) {
                log_info("Tool change aborted");
                return false;  // aborted
            }
            // probe the new tool
            move_to_save_z();
            move_over_toolsetter();

            if (!seek_probe())
                return false;

            float offset_probe[MAX_N_AXIS] = {};
            if (!probe(_probe_feed_rate, offset_probe)) {
                return false;
            }

            float tlo = offset_probe[2] - _tool_setter_offset;

            //log_info("Set TLO:" << tlo);
            gc_exec_linef(false, Uart0, "G43.1Z%0.3f", tlo);

            move_to_save_z();
            gc_exec_linef(false, Uart0, "G0X%0.3fY%0.3f", starting_wpos[0], starting_wpos[1]);
            gc_exec_linef(false, Uart0, "G0Z%0.3f", starting_wpos[2]);

            if (spindle_was_on) {
                gc_exec_linef(false, Uart0, "M3");  // spindle should handle spinup delay
            }

            return true;
        } catch (...) { log_info("Exception caught"); }

        return false;
    }

    void Manual_ATC::reset() {
        _is_OK                   = true;
        _have_tool_setter_offset = false;
        _prev_tool               = gc_state.tool;  // Double check this
    }

    void Manual_ATC::move_to_change_location() {
        move_to_save_z();                                                                                           // go to safe Z
        gc_exec_linef(false, Uart0, "G53G0X%0.3fY%0.3fZ%0.3f", _change_mpos[0], _change_mpos[1], _change_mpos[2]);  // go to change position
    }

    void Manual_ATC::move_to_save_z() {
        gc_exec_linef(false, Uart0, "G53G0Z%0.3f", _safe_z);
    }

    void Manual_ATC::move_over_toolsetter() {
        gc_exec_linef(false, Uart0, "G53G0X%0.3fY%0.3f", _ets_mpos[0], _ets_mpos[1]);
    }

    bool Manual_ATC::probe(float rate, float* probe_z_mpos) {
        gc_exec_linef(true, Uart0, "G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], rate);
        if (sys.state == State::Alarm) {
            return false;
        }
        motor_steps_to_mpos(probe_z_mpos, probe_steps);
        return true;
    }

    bool Manual_ATC::seek_probe() {
        gc_exec_linef(true, Uart0, "G53G0Z%0.3f", _ets_rapid_z_mpos);
        if (_probe_seek_rate > _probe_feed_rate) {
            float probe_mpos[6];
            if (!probe(_probe_seek_rate, probe_mpos)) {
                return false;
            }
            // retract
            gc_exec_linef(false, Uart0, "G53G0Z%0.3f", probe_mpos[2] + 5.0);
        }
        return true;
    }

    bool Manual_ATC::hold_and_wait_for_resume() {
        protocol_buffer_synchronize();
        protocol_send_event(&feedHoldEvent);

        log_info("Feedhold. Send resume after tool change.");
        protocol_handle_events();

        while (sys.state == State::Hold) {
            vTaskDelay(1);
            protocol_handle_events();  // do critical stuff
        }

        if (sys.state == State::Idle) {
            return true;
        }

        return false;
    }

    namespace {
        ATCFactory::InstanceBuilder<Manual_ATC> registration("atc_manual");
    }
}
