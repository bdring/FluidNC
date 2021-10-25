// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ESP32
#include "SerInPinDetail.h"
#include "Machine/SerInBus.h"
#include "Machine/MachineConfig.h"
#include <esp_attr.h>      // IRAM_ATTR
#include "../Assert.h"


namespace Pins
{
    SerInPinDetail::SerInPinDetail(pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index),
        _capabilities(PinCapabilities::Input | PinCapabilities::SERI | PinCapabilities::ISR),
        _attributes(Pins::PinAttributes::Undefined),
        _readWriteMask(0)
    {
        bool claimed = Machine::SerInBus::getPinsUsed() & (1 << index);
        Assert(index < Machine::SerInBus::s_max_pins-1, "Pin number is greater than max %d", Machine::SerInBus::s_max_pins-1);
        Assert(!claimed, "Pin is already used.");

        // User defined pin capabilities

        for (auto opt : options)
        {
            if (opt.is("low"))
            {
                 _attributes = _attributes | PinAttributes::ActiveLow;
            }
            else if (opt.is("high"))
            {
                // Default: Active HIGH.
            }
            else
            {
                Assert(false, "Unsupported SERI option '%s'", opt());
            }
        }

        Machine::SerInBus::setPinUsed(index);

        // readWriteMask is xor'ed with the value to invert it if active low
        _readWriteMask = _attributes.has(PinAttributes::ActiveLow);
    }


    PinCapabilities SerInPinDetail::capabilities() const
    {
        return
            PinCapabilities::Input |
            PinCapabilities::ISR |
            PinCapabilities::SERI;
    }


    void IRAM_ATTR SerInPinDetail::write(int high)
    {
        Assert(0,"write() to SERI Pins not allowed");
    }


    void SerInPinDetail::synchronousWrite(int high)
    {
        Assert(0,"synchronousWrite() to SERI Pins not allowed");
    }

    int SerInPinDetail::read()
    {
        uint32_t value = config->_seri->value();
        return ((value >> _index) & 1) ^ _readWriteMask;
    }


    void SerInPinDetail::setAttr(PinAttributes value)
    {
        Assert(!value.has(PinAttributes::Output), "SERI pins cannot be used as output");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the SERI pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this SERI pin have been set before, and there's a conflict.");
        _attributes = value;
    }


    void SerInPinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode)
    {
        Assert(mode == CHANGE);
        m_callback = callback;
        m_cb_arg = arg;
        config->_seri->attachFakeInterrupt(_index,this);
    }


    void SerInPinDetail::detachInterrupt()
    {
        config->_seri->detachFakeInterrupt(_index);
        m_callback = nullptr;
        m_cb_arg = nullptr;
    }


    String SerInPinDetail::toString()
    {
        auto s = String("SERI.") + int(_index);
        if (_attributes.has(PinAttributes::ActiveLow))
        {
            s += ":low";
        }
        return s;
    }

}   // namespace Pins

#endif
