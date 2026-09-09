// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "ControlPin.h"
#include "Machine/InputPin.h"
#include <vector>

using namespace Machine;
class Control : public Configuration::Configurable {
public:
    Control();

    std::vector<InputPin*> _pins;

    // Unlike the other entries in _pins, this has no bound Event and is not gated by
    // YAML config to work -- init() always registers it as a virtual pin (Channel.h),
    // so it is recognized on every channel regardless of configuration. It can
    // *additionally* be bound to a real Pin via "single_block_pin: ..." in config,
    // like any other _pins entry, since it still goes through group()/init() the same
    // way; that's independent of, and not required for, the virtual-pin behavior.
    InputPin _singleBlockPin { "single_block_pin", 'Q' };

    // Initializes control pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    bool stuck();
    bool safety_door_ajar();
    bool pins_block_unlock();

    std::string report_status();

    ~Control() = default;
};
