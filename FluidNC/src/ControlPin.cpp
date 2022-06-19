#include "ControlPin.h"

#include "Report.h"          // addPinReport
#include "Protocol.h"        // ExecAlarm
#include <esp_attr.h>        // IRAM_ATTR
#include <esp32-hal-gpio.h>  // CHANGE

void IRAM_ATTR ControlPin::handleISR() {
    bool pinState = _pin.read();
    _value        = pinState;

    // Rate limit control pin events so switch bounce does not cause multiple events
    if (pinState && (_debounceEnd == 0 || ((getCpuTicks() - _debounceEnd) >= 0))) {
        _debounceEnd = usToEndTicks(debounceUs);
        // We use 0 to mean that the debounce lockout is inactive,
        // so if the end time happens to be 0, bump it up by one tick.
        if (_debounceEnd == 0) {
            _debounceEnd = 1;
        }
        _rtVariable = true;
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

ControlPin::~ControlPin() {
    _pin.detachInterrupt();
}
