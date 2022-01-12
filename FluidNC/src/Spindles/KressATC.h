// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    
        return true for success
        automatic = false for manual tool change    
    bool tool_change(uint8_t new_tool(), bool automatic)

    void probe_notification()





*/
#include "OnOffSpindle.h"

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class KressATC : public OnOff {
    public:
        KressATC() = default;

        KressATC(const KressATC&) = delete;
        KressATC(KressATC&&)      = delete;
        KressATC& operator=(const KressATC&) = delete;
        KressATC& operator=(KressATC&&) = delete;

        void atc_init() override;
        bool tool_change(uint8_t new_tool, bool automatic) override;
        void probe_notification() override;

        ~KressATC() {}

        // Configuration handlers:

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "kress_atc"; }

    protected:
    };
}
