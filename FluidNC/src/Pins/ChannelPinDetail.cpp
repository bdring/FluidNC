// Copyright (c) 2023 -  MitchBradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ChannelPinDetail.h"

namespace Pins {
    ChannelPinDetail::ChannelPinDetail(Channel* channel, int number, const PinOptionsParser& options) :
        PinDetail(number), _channel(channel) {
        for (auto opt : options) {
            if (opt.is("pu")) {
                setAttr(PinAttributes::PullUp);
            } else if (opt.is("pd")) {
                setAttr(PinAttributes::PullDown);
            } else if (opt.is("low")) {
                setAttr(PinAttributes::ActiveLow);
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            }
        }
    }

    PinCapabilities ChannelPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::PWM | PinCapabilities::Void;
    }

    void ChannelPinDetail::write(int high) {
        if (high == _value) {
            return;
        }
        _value        = high;
        std::string s = "io.";
        s += std::to_string(_index);
        s += "=";
        s += std::to_string(high);
        _channel->out(s, "SET:");
    }
    int  ChannelPinDetail::read() { return _value; }
    void ChannelPinDetail::setAttr(PinAttributes attr) {
        _attributes = _attributes | attr;

        std::string s = "io.";
        s += std::to_string(_index);
        s += "=";
        if (_attributes.has(Pins::PinAttributes::Input)) {
            s += "inp";
        } else if (_attributes.has(Pins::PinAttributes::Output)) {
            s += "out";
        } else {
            return;
        }

        if (_attributes.has(Pins::PinAttributes::PullUp)) {
            s += ":pu";
        }
        if (_attributes.has(Pins::PinAttributes::PullDown)) {
            s += ":pd";
        }
        if (_attributes.has(Pins::PinAttributes::ActiveLow)) {
            s += ":low";
        }

        _channel->setAttr(_index, _attributes.has(Pins::PinAttributes::Input) ? &this->_value : nullptr, s, "INI:");
    }
    PinAttributes ChannelPinDetail::getAttr() const { return _attributes; }
    std::string   ChannelPinDetail::toString() {
        std::string s = _channel->name();
        s += ".";
        s += std::to_string(_index);
        if (_attributes.has(PinAttributes::ActiveLow)) {
            s += ":low";
        }
        if (_attributes.has(PinAttributes::PullUp)) {
            s += ":pu";
        }
        if (_attributes.has(PinAttributes::PullDown)) {
            s += ":pd";
        }
        return s;
    }

    void ChannelPinDetail::registerEvent(EventPin* obj) { _channel->registerEvent(_index, obj); }
}
