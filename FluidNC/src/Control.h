// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "ControlPin.h"

class Control : public Configuration::Configurable {
// private:
    // TODO: Should we not just put this in an array so we can enumerate it easily?
public:
    ControlPin _safetyDoor;
    ControlPin _reset;
    ControlPin _feedHold;
    ControlPin _cycleStart;
    ControlPin _macro0;
    ControlPin _macro1;
    ControlPin _macro2;
    ControlPin _macro3;

public:
    Control();

    // Initializes control pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    bool   stuck();
    bool   system_check_safety_door_ajar();
    String report();

    ~Control() = default;
};
