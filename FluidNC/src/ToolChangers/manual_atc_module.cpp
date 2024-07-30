#include "manual_atc_module.h"

#include "../Machine/MachineConfig.h"

void Manual_ATC::afterParse() {}

void Manual_ATC::init() {
    log_info("ATC: atc_manual");
}

ModuleFactory::InstanceBuilder<Manual_ATC> atc __attribute__((init_priority(104))) ("atc_manual");
