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

        // Configuration handlers: no fields of its own -- exists so this class
        // has its own real group() for the doc generator to find (see
        // tools/build_config_docs.py's SECTIONS table), matching PWM/Laser/
        // BESC/etc.'s own-file-per-type convention, rather than Relay silently
        // sharing OnOff's config_items.yaml section (or having none at all).
        // No @default_for override needed here -- Relay never overrides
        // OnOff::init()'s speed_map default (unlike Dac, see DacSpindle.h),
        // so it correctly inherits OnOff's own @default_for as-is.
        void group(Configuration::HandlerBase& handler) override { OnOff::group(handler); }

    protected:
    };
}
