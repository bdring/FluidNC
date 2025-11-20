// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "GCode.h"
#include "InputPin.h"

namespace Machine {

    class UserInputs : public Configuration::Configurable {
        // TODO - analog pins are read the same as digital. The Pin
        // API should either be extended to support analog reads, or
        // a new AnalogPin class should be created.

    public:
        UserInputs();
        virtual ~UserInputs();

        static InputPin digitalInput[];
        // Should be AnalogPin but we do not have such a thing yet
        static InputPin analogInput[];

        void init();
        void group(Configuration::HandlerBase& handler) override;
    };

}  // namespace Machine
