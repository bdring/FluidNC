// Copyright (c) 2023 -  MitchBradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ChannelPinDetail.h"

namespace Pins {
    ChannelPinDetail::ChannelPinDetail(Channel* channel, int number, const PinOptionsParser& options) :
        PinDetail(number), _channel(channel) {}

    PinCapabilities ChannelPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::Void;
    }

    void          ChannelPinDetail::write(int high) { _channel->out(_index, high); }
    int           ChannelPinDetail::read() { return _channel->in(_index); }
    void          ChannelPinDetail::setAttr(PinAttributes value) { _channel->setAttr(_index, value); }
    PinAttributes ChannelPinDetail::getAttr() const { return _channel->getAttr(_index); }
    std::string   ChannelPinDetail::toString() {
        std::string s = _channel->name();
        s += ".";
        s += std::to_string(_index);
        return s;
    }

    void ChannelPinDetail::registerEvent(EventPin* obj) { _channel->registerEvent(_index, obj); }
}
