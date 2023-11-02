// Copyright (c) 2023 -  MitchBradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ChannelPinDetail.h"

namespace Pins {
    ChannelPinDetail::ChannelPinDetail(Channel* channel, int number, const PinOptionsParser& options) :
        PinDetail(number), _channel(channel) {
        for (auto opt : options) {
            if (opt.is("pu")) {
                _channel->setAttr(_index, PinAttributes::PullUp);
            } else if (opt.is("pd")) {
                _channel->setAttr(_index, PinAttributes::PullDown);
            } else if (opt.is("low")) {
                _channel->setAttr(_index, PinAttributes::ActiveLow);
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            }
        }
    }

    PinCapabilities ChannelPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::PWM | PinCapabilities::Void;
    }

    void ChannelPinDetail::write(int high) {
        _channel->out(_index, high);
    }
    int ChannelPinDetail::read() {
        return _channel->in(_index);
    }
    void ChannelPinDetail::setAttr(PinAttributes value) {
        _channel->setAttr(_index, value);
        std::string s = "[MSG:INI: io.";
        s += std::to_string(_index);
        s += "=";
        if (value.has(PinAttributes::Input)) {
            s += "inp";
        } else if (_channel->getAttr(_index) & PinAttributes::Output) {
            s += "out";
        } else {
            return;
        }

        if (_channel->getAttr(_index) & PinAttributes::PullUp) {
            s += ":pu";
        }
        if (_channel->getAttr(_index) & PinAttributes::PullDown) {
            s += ":pu";
        }
        if (_channel->getAttr(_index) & PinAttributes::ActiveLow) {
            s += ":low";
        }

        s += "]";
        _channel->println(s.c_str());
    }
    PinAttributes ChannelPinDetail::getAttr() const {
        return _channel->getAttr(_index);
    }
    std::string ChannelPinDetail::toString() {
        std::string s = _channel->name();
        s += ".";
        s += std::to_string(_index);
        if (_channel->getAttr(_index) & PinAttributes::ActiveLow) {
            s += ":low";
        }
        if (_channel->getAttr(_index) & PinAttributes::PullUp) {
            s += ":pu";
        }
        if (_channel->getAttr(_index) & PinAttributes::PullDown) {
            s += ":pd";
        }
        return s;
    }

    void ChannelPinDetail::registerEvent(EventPin* obj) {
        _channel->registerEvent(_index, obj);
    }
}
