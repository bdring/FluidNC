// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ESP32
#include "I2SIPinDetail.h"
#include "Machine/I2SIBus.h"
#include "Machine/MachineConfig.h"
#include <esp_attr.h>      // IRAM_ATTR
#include "../Assert.h"


namespace Pins
{
    I2SIPinDetail::I2SIPinDetail(pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index),
        _capabilities(PinCapabilities::Input | PinCapabilities::I2SI | PinCapabilities::ISR),
        _attributes(Pins::PinAttributes::Undefined),
        _readWriteMask(0)
    {
        bool claimed = Machine::I2SIBus::getPinsUsed() & (1 << index);
        Assert(index < Machine::I2SIBus::s_max_pins-1, "Pin number is greater than max %d", Machine::I2SIBus::s_max_pins-1);
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
                Assert(false, "Unsupported I2SI option '%s'", opt());
            }
        }

        Machine::I2SIBus::setPinUsed(index);

        // readWriteMask is xor'ed with the value to invert it if active low
        _readWriteMask = _attributes.has(PinAttributes::ActiveLow);
    }


    PinCapabilities I2SIPinDetail::capabilities() const
    {
        return
            PinCapabilities::Input |
            PinCapabilities::ISR |
            PinCapabilities::I2SI;
    }


    void IRAM_ATTR I2SIPinDetail::write(int high)
    {
        Assert(0,"write() to I2SI Pins not allowed");
    }


    void I2SIPinDetail::synchronousWrite(int high)
    {
        Assert(0,"synchronousWrite() to I2SI Pins not allowed");
    }

    int IRAM_ATTR I2SIPinDetail::read()
    {
        uint32_t value = config->_i2si->value();
        uint32_t retval = ((value >> _index) & 1) ^ _readWriteMask;
        // log_debug("read(" << _index << ") got value=" << value << " returning " << retval);
        return retval;
    }


    void I2SIPinDetail::setAttr(PinAttributes value)
    {
        Assert(!value.has(PinAttributes::Output), "I2SI pins cannot be used as output");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the I2SI pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this I2SI pin have been set before, and there's a conflict.");
        _attributes = value;
    }


    void I2SIPinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode)
    {
        // log_debug("attach interrupt " << _index);
        Assert(mode == CHANGE);
        m_callback = callback;
        m_cb_arg = arg;
        config->_i2si->attachInterrupt(_index,this);
    }


    void I2SIPinDetail::detachInterrupt()
    {
        config->_i2si->detachInterrupt(_index);
        m_callback = nullptr;
        m_cb_arg = nullptr;
    }


    String I2SIPinDetail::toString()
    {
        auto s = String("I2SI.") + int(_index);
        if (_attributes.has(PinAttributes::ActiveLow))
        {
            s += ":low";
        }
        return s;
    }

}   // namespace Pins

#endif
