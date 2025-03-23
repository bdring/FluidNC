// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "PinOptionsParser.h"
#include "src/UartChannel.h"
#include "../Logging.h"

namespace Pins {
    class ChannelPinDetail : public PinDetail {
    private:
        UartChannel*  _channel;
        PinAttributes _attributes;
        bool          _value = false;

    public:
        ChannelPinDetail(UartChannel* channel, int number, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value, uint32_t frequency = 0) override;
        PinAttributes getAttr() const override;
        void          setDuty(uint32_t duty) override;
        uint32_t      maxDuty() override;

        void registerEvent(InputPin* obj) override;

        std::string toString() override;

        ~ChannelPinDetail() override {}
    };
}
