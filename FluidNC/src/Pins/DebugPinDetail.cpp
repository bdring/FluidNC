// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "DebugPinDetail.h"

#include "../UartChannel.h"
#include <esp32-hal.h>  // millis()

namespace Pins {

    // I/O:
    void DebugPinDetail::write(int high) {
        if (high != int(_isHigh)) {
            _isHigh = bool(high);
            if (shouldEvent()) {
                log_msg_to(Uart0, "Write " << toString() << " < " << high);
            }
        }
        _implementation->write(high);
    }

    int DebugPinDetail::read() {
        auto result = _implementation->read();
        if (shouldEvent()) {
            log_msg_to(Uart0, "Read  " << toString() << " > " << result);
        }
        return result;
    }
    void DebugPinDetail::setAttr(PinAttributes value) {
        char buf[10];
        int  n = 0;
        if (value.has(PinAttributes::Input)) {
            buf[n++] = 'I';
        }
        if (value.has(PinAttributes::Output)) {
            buf[n++] = 'O';
        }
        if (value.has(PinAttributes::PullUp)) {
            buf[n++] = 'U';
        }
        if (value.has(PinAttributes::PullDown)) {
            buf[n++] = 'D';
        }
        if (value.has(PinAttributes::ISR)) {
            buf[n++] = 'E';
        }
        if (value.has(PinAttributes::Exclusive)) {
            buf[n++] = 'X';
        }
        if (value.has(PinAttributes::InitialOn)) {
            buf[n++] = '+';
        }
        buf[n++] = '\0';

        if (shouldEvent()) {
            log_msg_to(Uart0, "Set pin attr " << toString() << " = " << buf);
        }
        _implementation->setAttr(value);
    }

    PinAttributes DebugPinDetail::getAttr() const { return _implementation->getAttr(); }

    void DebugPinDetail::CallbackHandler::handle(void* arg) {
        auto handler = static_cast<CallbackHandler*>(arg);
        if (handler->_myPin->shouldEvent()) {
            log_msg_to(Uart0, "Received ISR on " << handler->_myPin->toString());
        }
        handler->callback(handler->argument);
    }

    // ISR's:
    void DebugPinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode) {
        _isrHandler._myPin   = this;
        _isrHandler.argument = arg;
        _isrHandler.callback = callback;

        if (shouldEvent()) {
            log_msg_to(Uart0, "Attaching interrupt to pin " << toString() << ", mode " << mode);
        }
        _implementation->attachInterrupt(_isrHandler.handle, &_isrHandler, mode);
    }
    void DebugPinDetail::detachInterrupt() { _implementation->detachInterrupt(); }

    bool DebugPinDetail::shouldEvent() {
        // This method basically ensures we don't flood users:
        auto time = millis();

        if ((time - _lastEvent) > 1000) {
            _lastEvent  = time;
            _eventCount = 1;
            return true;
        } else if (_eventCount < 10) {
            _lastEvent = time;
            ++_eventCount;
            return true;
        } else if (_eventCount == 10) {
            _lastEvent = time;
            ++_eventCount;
            log_msg_to(Uart0, "Suppressing events...");
            return false;
        } else {
            _lastEvent = time;
            return false;
        }
    }
}
