#include "ControlPin.h"
#include "Control.h"

#include "Report.h"    // addPinReport
#include "Protocol.h"  // event_queue
#include <esp_attr.h>  // IRAM_ATTR

void IRAM_ATTR ControlPin::handleISR() {
    Event* evt = this;
    xQueueSendFromISR(event_queue, &evt, NULL);
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
    _pin.attachInterrupt(ISRHandler, Pin::EITHER_EDGE, this);
}

ControlPin::~ControlPin() {
    _pin.detachInterrupt();
}
