// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "DebugPinDetail.h"
#include "Driver/Console.h"

namespace Pins {

    // I/O:
    void IRAM_ATTR DebugPinDetail::write(int32_t high) {
        if (high != int(_isHigh)) {
            _isHigh = bool(high);
            if (shouldEvent()) {
                log_msg_to(Console, "Write " << toString() << " < " << high);
            }
        }
        _implementation->write(high);
    }

    int32_t DebugPinDetail::read() {
        auto result = _implementation->read();
        if (shouldEvent()) {
            log_msg_to(Console, "Read  " << toString() << " > " << result);
        }
        return result;
    }
    void DebugPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        char    buf[10];
        int32_t n = 0;
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
            log_msg_to(Console, "Set pin attr " << toString() << " = " << buf);
        }
        _implementation->setAttr(value);
    }
#if 0
    void DebugPinDetail::CallbackHandler::handle(void* arg, bool v) {
        auto handler = static_cast<CallbackHandler*>(arg);
        if (handler->_myPin->shouldEvent()) {
            log_msg_to(Console, "Received ISR on " << handler->_myPin->toString());
        }
        handler->callback(handler->argument, v);
    }
#endif
    PinAttributes DebugPinDetail::getAttr() const {
        return _implementation->getAttr();
    }
}
