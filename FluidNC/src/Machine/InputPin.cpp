#include "InputPin.h"
#include "Report.h"

#include "Protocol.h"  // protocol_send_event

void InputPin::init() {
    if (undefined()) {
        return;
    }
    report(_legend);
    setAttr(Pin::Attr::Input);  // Setup the pin first
    // Register the event after pin setup so the initial state is correct
    registerEvent(this);
    update(read());
}

void InputPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    report_recompute_pin_string();
}
