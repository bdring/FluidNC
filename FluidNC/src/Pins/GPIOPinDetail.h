// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "Driver/PwmPin.h"

namespace Pins {
    class GPIOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;

        static PinCapabilities GetDefaultCapabilities(pinnum_t index);

        static std::vector<bool> _claimed;

        bool _lastWrittenValue = false;

        static void gpioAction(int, void*, int);
        PwmPin*     _pwm;

        int8_t _driveStrength = -1;

        void setDriveStrength(int n, PinAttributes attr);

    public:
        static const int nGPIOPins = 40;

        GPIOPinDetail(pinnum_t index, PinOptionsParser options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        void     setDuty(uint32_t duty) override;
        uint32_t maxDuty() override { return _pwm->period(); };

        int8_t driveStrength() { return _driveStrength; }

        bool canStep() override { return true; }

        void registerEvent(InputPin* obj) override;

        std::string toString() override;

        ~GPIOPinDetail() override { _claimed[_index] = false; }
    };

}
