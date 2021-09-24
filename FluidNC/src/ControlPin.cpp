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
    _pin.attachInterrupt<ControlPin, &ControlPin::handleISR>(this, CHANGE);
    _rtVariable = false;
    _value      = _pin.read();
    // Control pins must start in inactive state
    if (_value) {
        log_error(_legend << " pin is active at startup");
        rtAlarm = ExecAlarm::ControlPin;
    }
}

String ControlPin::report() {
    return get() ? String(_letter) : String("");
}

ControlPin::~ControlPin() {
    _pin.detachInterrupt();
}
