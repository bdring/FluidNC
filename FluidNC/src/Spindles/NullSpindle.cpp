// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used when you don't want to use a spindle No I/O will be used
    and most methods don't do anything
*/
#include "NullSpindle.h"

#include "System.h"  // sys.spindle_speed

namespace Spindles {
    // ======================= Null ==============================
    // Null is just a bunch of do nothing (ignore) methods to be used when you don't want a spindle

    void Null::init() {
        is_reversable = false;
        config_message();
        _speeds.clear();
    }
    void IRAM_ATTR Null::setSpeedfromISR(uint32_t dev_speed) {};

    void Null::setState(SpindleState state, SpindleSpeed speed) {
        _current_state = state;
        sys.set_spindle_speed(speed);
    }
    void Null::config_message() { /*log_info("No spindle");*/
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Null> registration("NoSpindle");
    }
}
