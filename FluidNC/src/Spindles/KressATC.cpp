// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*

*/

#include "KressATC.h"

// ========================= KressATC ==================================

namespace Spindles {
    void KressATC::atc_init() { log_info("ATC Init"); }

    bool KressATC::tool_change(uint8_t new_tool, bool automatic) {
        log_info(name() << ": Tool change tool num:" << new_tool << " Auto:" << automatic);
        return true;
    }

    void KressATC::probe_notification() { log_info(name() << ": Probe notification"); }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<KressATC> registration("kress_atc");
    }
}
