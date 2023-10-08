#include "EventPin.h"

#include "src/Report.h"    // addPinReport
#include "src/Protocol.h"  // event_queue
#include "src/System.h"    // sys

EventPin::EventPin(Event* event, const char* legend) : _event(event), _legend(legend) {}

void EventPin::trigger(bool active) {
    update(active);
    if (active) {
        protocol_send_event(_event, this);
    }
}
