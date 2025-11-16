#include "EventPin.h"
#include "Report.h"

#include "Protocol.h"  // protocol_send_event

void EventPin::trigger(bool active) {
    InputPin::trigger(active);
    if (active) {
        if (state_is(State::Starting)) {
            if (_alarm != ExecAlarm::None) {
                send_alarm(_alarm);
            }
        } else {
            protocol_send_event(_event, this);
        }
    }
}
