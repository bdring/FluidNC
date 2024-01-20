#include "EventPin.h"
#include "src/Report.h"

#include "src/Protocol.h"  // protocol_send_event

EventPin::EventPin(Event* event, const char* legend) : _event(event), _legend(legend) {}

void EventPin::trigger(bool active) {
    update(active);
    log_debug(_legend << " " << active);
    if (active) {
        protocol_send_event(_event, this);
    }
    report_recompute_pin_string();
}
