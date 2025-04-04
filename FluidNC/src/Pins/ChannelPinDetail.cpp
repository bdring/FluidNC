// Copyright (c) 2023 -  MitchBradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ChannelPinDetail.h"

namespace Pins {
    ChannelPinDetail::ChannelPinDetail(UartChannel* channel, int number, const PinOptionsParser& options) :
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

    void IRAM_ATTR ChannelPinDetail::write(int high) {
        if (high == _value) {
            return;
        }
        _value = high;
        //        _channel->write(high ? 0xC5 : 0xC4);
        //        _channel->write(0x80 + _index);
        _channel->writeUTF8(_index + (high ? Channel::PinHighFirst : Channel::PinLowFirst));
    }
    uint32_t ChannelPinDetail::maxDuty() {
        return 1000;
    }
    void IRAM_ATTR ChannelPinDetail::setDuty(uint32_t duty) {
        _channel->writeUTF8(0x10000 + (_index << 10) + duty);
    }

    int ChannelPinDetail::read() {
        return _value;
    }
    void ChannelPinDetail::setAttr(PinAttributes attr, uint32_t frequency) {
        _attributes = _attributes | attr;

        std::string s = "io.";
        s += std::to_string(_index);
        s += "=";
        if (_attributes.has(Pins::PinAttributes::PWM)) {
            s += "pwm,frequency=";
            s += std::to_string(frequency);
        } else if (_attributes.has(Pins::PinAttributes::Input)) {
            s += "in";
            if (_attributes.has(Pins::PinAttributes::PullUp)) {
                s += ",pu";
            }
            if (_attributes.has(Pins::PinAttributes::PullDown)) {
                s += ",pd";
            }
        } else if (_attributes.has(Pins::PinAttributes::Output)) {
            s += "out";
        } else {
            return;
        }
        if (_attributes.has(Pins::PinAttributes::ActiveLow)) {
            s += ",low";
        }

        // The second parameter is used to maintain a list of pin values in the Channel
        Assert(_channel->setAttr(_index, _attributes.has(Pins::PinAttributes::Input) ? &this->_value : nullptr, s),
               "Expander pin configuration failed: %s %s",
               _channel->name().c_str(),
               s.c_str());
    }
    PinAttributes ChannelPinDetail::getAttr() const {
        return _attributes;
    }
    std::string ChannelPinDetail::toString() {
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

    void ChannelPinDetail::registerEvent(InputPin* obj) {
        _channel->registerEvent(_index, obj);
    }
}
