#include "EventPin.h"
#include "Report.h"

#include "Protocol.h"  // protocol_send_event

void EventPin::trigger(bool active) {
    InputPin::trigger(active);
    if (active) {
        protocol_send_event(_event, this);
    }
}
