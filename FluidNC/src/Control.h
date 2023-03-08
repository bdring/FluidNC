// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "ControlPin.h"
#include <vector>

using namespace Machine;
class Control : public Configuration::Configurable {
public:
    Control();

    std::vector<ControlPin*> _pins;

    // Initializes control pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    bool stuck();
    bool safety_door_ajar();

    std::string report_status();

    bool startup_check();

    ~Control() = default;
};
