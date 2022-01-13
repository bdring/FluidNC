// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*

*/

#include "KressATC.h"
#include "../Protocol.h"
#include "../GCode.h"

// ========================= KressATC ==================================

namespace Spindles {
    void KressATC::atc_init() { log_info("ATC Init"); }

    bool KressATC::tool_change(uint8_t new_tool, bool pre_select) {
        float saved_mpos[MAX_N_AXIS] = {};  // the position before the tool change

        log_info(name() << ": Tool change tool num:" << new_tool << " Auto:" << pre_select);

        if (new_tool == current_tool) {
            log_info("ATC existing tool requested:" << new_tool);
            return true;
        }

        // new tool should never be above tool count because of earlier spindle check

        protocol_buffer_synchronize();  // wait for all previous moves to complete
        motor_steps_to_mpos(saved_mpos, motor_steps);

        // see if we need to switch out of incremental (G91) mode

        // is spindle on? Turn it off and determine when the spin down should be done.

        // ============= Start of tool change ====================

        //go_above_tool(new_tool);

        // If the spindle was on before we started, we need to turn it back on.

        // return to saved mpos in XY

        // return to saved mpos in Z if it is not outside of work area.

        // was was_incremental on? If so, return to that state

        // Wait for spinup

        return true;
    }

    void KressATC::probe_notification() { log_info(name() << ": Probe notification"); }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<KressATC> registration("kress_atc");
    }
}
