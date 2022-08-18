// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "src/System.h"                 // sys
#include "src/Machine/MachineConfig.h"  // config

void MacroEvent::run(int arg) {
    if (sys.state != State::Idle) {
        log_error("Macro can only be used in idle state");
        return;
    }
    log_debug("Macro " << _num);
    config->_macros->run_macro(_num);
}

MacroEvent macro0Event { 0 };
MacroEvent macro1Event { 1 };
MacroEvent macro2Event { 2 };
MacroEvent macro3Event { 3 };
