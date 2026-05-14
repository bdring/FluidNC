#include "InputPin.h"
#include "Report.h"

#include "Protocol.h"  // protocol_send_event

void InputPin::init() {
    if (undefined()) {
        return;
    }
    report(_legend);
    // Register the event first because some pin types send an
    // initial pin state report upon setAttr()
    registerEvent(this);
    setAttr(Pin::Attr::Input);
    update(read());
}

void InputPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    report_recompute_pin_string();
}
