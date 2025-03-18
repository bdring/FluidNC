#include "EventPin.h"
#include "src/Report.h"

#include "src/Protocol.h"  // protocol_send_event

void EventPin::init() {
    if (_pin.undefined()) {
        return;
    }
    _pin.report(_legend);
    _pin.registerEvent(this);
    _pin.setAttr(Pin::Attr::Input);
    update(_pin.read());
}

void EventPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    if (active) {
        protocol_send_event(_event, this);
    }
    report_recompute_pin_string();
}

void InputPin::init() {
    if (undefined()) {
        return;
    }
    report(_legend);
    //    registerEvent(this);
    setAttr(Pin::Attr::Input);
    update(read());
}

void InputPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    report_recompute_pin_string();
}
