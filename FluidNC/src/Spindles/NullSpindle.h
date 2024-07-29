// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is used when you don't want to use a spindle No I/O will be used
	and most methods don't do anything
*/

#include "Spindle.h"

namespace Spindles {
    // This is a dummy spindle that has no I/O.
    // It is used to ignore spindle commands when no spindle is desired
    class Null : public Spindle {
    public:
        Null(const char* name) : Spindle(name) {}

        Null(const Null&)            = delete;
        Null(Null&&)                 = delete;
        Null& operator=(const Null&) = delete;
        Null& operator=(Null&&)      = delete;

        void init() override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {}

        ~Null() {}
    };
}
