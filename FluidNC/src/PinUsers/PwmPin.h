// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Pin.h"
#include "../Assert.h"

namespace PinUsers {
    class PwmDetail {
    public:
        virtual uint32_t getFrequency() const = 0;
        virtual uint32_t getMaxDuty() const   = 0;

        // Sets the PWM value from 0..1.
        virtual void setValue(float value) = 0;

        virtual ~PwmDetail() {}
    };

    class PwmPin {
        Pin        _pin;
        PwmDetail* _detail;

    public:
        PwmPin() : _detail(nullptr) {}
        PwmPin(Pin&& pin, uint32_t frequency, uint32_t maxDuty);

        // Returns actual frequency which might not be exactly the same as requested(nearest supported value)
        inline uint32_t getFrequency() const { return _detail->getFrequency(); }

        // Returns actual maxDuty which might not be exactly the same as requested(nearest supported value)
        inline uint32_t getMaxDuty() const { return _detail->getMaxDuty(); }

        inline void setValue(float value) const { return _detail->setValue(value); }

        inline ~PwmPin() { delete _detail; }
    };
}
