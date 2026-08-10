// Copyright (c) 2023 -  MitchBradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannelPinDetail.h"
#include "WebUI/WSChannel.h"

namespace Pins {
    WSChannelPinDetail::WSChannelPinDetail(pinnum_t index, const PinOptionsParser& options) : PinDetail(index) {
        _name = "ws";
        _name += ".";
        _name += std::to_string(index);

        for (auto opt : options) {
            if (opt.is("pu")) {
                setAttr(PinAttributes::PullUp);
                _name += ":pu";
            } else if (opt.is("pd")) {
                setAttr(PinAttributes::PullDown);
                _name += ":pd";
            } else if (opt.is("low")) {
                setAttr(PinAttributes::ActiveLow);
                _name += ":low";
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            }
        }
    }

    PinCapabilities WSChannelPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::PWM | PinCapabilities::Void;
    }

    void IRAM_ATTR WSChannelPinDetail::write(bool high) {
        if (high == _value) {
            return;
        }
        _value = high;
#if LATER
        WSChannels::writeUTF8(_index + (high ? Channel::PinHighFirst : Channel::PinLowFirst));
#endif
    }

    uint32_t WSChannelPinDetail::maxDuty() {
        return 1000;
    }
    void IRAM_ATTR WSChannelPinDetail::setDuty(uint32_t duty) {
#if LATER
        WSChannels::writeUTF8(0x10000 + (_index << 10) + duty);
#endif
    }

    bool WSChannelPinDetail::read() {
        return _value;
    }
    void WSChannelPinDetail::setAttr(PinAttributes attr, uint32_t frequency) {
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

#if LATER
        // The second parameter is used to maintain a list of pin values in the Channel
        Assert(WSChannels::setAttr(_index, _attributes.has(Pins::PinAttributes::Input) ? &this->_value : nullptr, s),
               "Expander pin configuration failed: %s %s",
               "ws",
               s.c_str());
#endif
    }
    PinAttributes WSChannelPinDetail::getAttr() const {
        return _attributes;
    }

    void WSChannelPinDetail::registerEvent(InputPin* obj) {
        WebUI::WSChannels::registerEvent(_index, obj);
    }
}
