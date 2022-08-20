// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "../Pins/PinAttributes.h"

#include "../Platform.h"

namespace Extenders {
    class PinExtenderDriver : public Configuration::Configurable {
    public:
        virtual void init() = 0;

        virtual void claim(pinnum_t index) = 0;
        virtual void free(pinnum_t index)  = 0;

        virtual void IRAM_ATTR setupPin(pinnum_t index, Pins::PinAttributes attr) = 0;
        virtual void IRAM_ATTR writePin(pinnum_t index, bool high)                = 0;
        virtual bool IRAM_ATTR readPin(pinnum_t index)                            = 0;
        virtual void IRAM_ATTR flushWrites()                                      = 0;

        virtual void attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode);
        virtual void detachInterrupt(pinnum_t index);

        // Name is required for the configuration factory to work.
        virtual const char* name() const = 0;

        // Virtual base classes require a virtual destructor.
        virtual ~PinExtenderDriver() {}
    };
}
