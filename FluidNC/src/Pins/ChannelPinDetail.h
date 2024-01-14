// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "PinOptionsParser.h"
#include "src/Channel.h"
#include "../Logging.h"

namespace Pins {
    class ChannelPinDetail : public PinDetail {
    private:
        Channel*      _channel;
        PinAttributes _attributes;
        bool          _value = false;

    public:
        ChannelPinDetail(Channel* channel, int number, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        void registerEvent(EventPin* obj);

        std::string toString() override;

        ~ChannelPinDetail() override {}
    };
}
