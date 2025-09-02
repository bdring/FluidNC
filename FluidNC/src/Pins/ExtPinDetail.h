// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "Extenders/PinExtenderDriver.h"
#include "Configuration/Configurable.h"
#include "Machine/MachineConfig.h"

#include <bitset>
#include <string>

namespace Pins {
    class ExtPinDetail : public PinDetail {
        Extenders::PinExtenderDriver* _owner = nullptr;
        int                           _device;

        PinCapabilities _capabilities;
        PinAttributes   _attributes;

    public:
        ExtPinDetail(int device, pinnum_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void write(int high) override;
        void synchronousWrite(int high) override;
        int  read() override;

#if 0
        // ISR's:
        void attachInterrupt(void (*callback)(void*, bool), void* arg, int mode) override;
        void detachInterrupt() override;
#endif

        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        std::string toString() override;

        ~ExtPinDetail() override;
    };
}
