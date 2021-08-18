#include "ControlPin.h"

#include "Report.h"          // addPinReport
#include "Protocol.h"        // ExecAlarm
#include <esp_attr.h>        // IRAM_ATTR
#include <esp32-hal-gpio.h>  // CHANGE

void IRAM_ATTR ControlPin::handleISR() {
    bool pinState = _pin.read();
    _value        = pinState;
    _rtVariable   = pinState;
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
}

void ControlPin::report(char* status) {
    if (get()) {
        addPinReport(status, _letter);
    }
}

ControlPin::~ControlPin() {
    _pin.detachInterrupt();
}
