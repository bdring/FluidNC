#include "atc_manual.h"

#include "../Machine/MachineConfig.h"

namespace ATCs {
    void Manual_ATC::init() {
        log_info("ATC:" << name());
    }

    void Manual_ATC::probe_notification() {}

    void Manual_ATC::tool_change(uint8_t value, bool pre_select) {}

    namespace {
        ATCFactory::InstanceBuilder<Manual_ATC> registration("atc_manual");
    }
}
