#include "EventPin.h"

#include "src/Protocol.h"  // protocol_send_event

EventPin::EventPin(Event* event, const char* legend) : _event(event), _legend(legend) {}

void EventPin::trigger(bool active) {
    update(active);
    if (active) {
        protocol_send_event(_event, this);
    }
}
