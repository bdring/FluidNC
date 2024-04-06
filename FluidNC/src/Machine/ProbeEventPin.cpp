#if 0
#include "src/Machine/ProbeEventPin.h"
#include "src/Machine/MachineConfig.h"  // config

#include "src/Protocol.h"  // protocol_send_event_from_ISR()

namespace Machine {
    ProbeEventPin::ProbeEventPin(const char* legend, Pin& pin) :
        EventPin(&probeEvent, legend), _pin(&pin) {}

    void ProbeEventPin::init() {
        if (_pin->undefined()) {
            return;
        }
        _value = _pin->read();
        _pin->report(_legend);
        _pin->setAttr(Pin::Attr::Input);
        _pin->registerEvent(static_cast<EventPin*>(this));
    }
    void ProbeEventPin::update(bool state) { _value = state; }
    bool ProbeEventPin::get() { return _value; }

}
#endif