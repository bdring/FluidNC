// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is used for a basic on/off spindle All S Values above 0
	will turn the spindle on.
*/
#include "OnOffSpindle.h"

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class Relay : public OnOff {
    public:
        Relay(const char* name) : OnOff(name) {}

        Relay(const Relay&)            = delete;
        Relay(Relay&&)                 = delete;
        Relay& operator=(const Relay&) = delete;
        Relay& operator=(Relay&&)      = delete;

        ~Relay() {}

    protected:
    };
}
