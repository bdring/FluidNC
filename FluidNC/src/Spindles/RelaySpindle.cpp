// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for a basic on/off spindle All S Values above 0
    will turn the spindle on.
*/

#include "RelaySpindle.h"

// ========================= Relay ==================================

namespace Spindles {
    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Relay> registration("Relay");
    }
}
