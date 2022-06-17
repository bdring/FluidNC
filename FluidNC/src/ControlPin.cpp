#include "ControlPin.h"

#include "Report.h"          // addPinReport
#include "Protocol.h"        // ExecAlarm
#include <esp_attr.h>        // IRAM_ATTR
#include <esp32-hal-gpio.h>  // CHANGE

void IRAM_ATTR ControlPin::handleISR() {
    bool pinState = _pin.read();
    _value        = pinState;
    if (pinState) {
        _rtVariable = pinState;
    }
}

void ControlPin::init() {
    if (_pin.undefined()) {
        return;
    }
    _pin.report(_legend);
    auto attr = Pin::Attr::Input | Pin::Attr::ISR;
    if (_pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
        attr = attr | Pin::Attr::PullUp;
    }
    _pin.setAttr(attr);
    _pin.attachInterrupt(ISRHandler, CHANGE, this);
    _rtVariable = false;
    _value      = _pin.read();
}

bool ControlPin::startup_check() {
    if (_value) {
        log_error(_legend << " pin is active at startup");
        return true;
    }
    return false;
}

String ControlPin::report() {
    return get() ? String(_letter) : String("");
}

ControlPin::~ControlPin() {
    _pin.detachInterrupt();
}
