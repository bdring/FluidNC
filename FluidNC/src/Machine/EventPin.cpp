#include "EventPin.h"
#include "src/Report.h"

#include "src/Protocol.h"  // protocol_send_event

void InputPin::init() {
    if (undefined()) {
        return;
    }
    report(_legend);
    registerEvent(this);
    setAttr(Pin::Attr::Input);
    update(read());
}

void InputPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    report_recompute_pin_string();
}

void EventPin::trigger(bool active) {
    InputPin::trigger(active);
    if (active) {
        protocol_send_event(_event, this);
    }
}
