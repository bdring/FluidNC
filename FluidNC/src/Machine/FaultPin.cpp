#include "src/Machine/FaultPin.h"
#include "src/Machine/MachineConfig.h"  // config

#include "src/MotionControl.h"  // mc_reset
#include "src/Protocol.h"       // protocol_send_event_from_ISR()

namespace Machine {
    FaultPin::FaultPin(Pin& pin) : EventPin(&faultPinEvent, "Fault", &pin) {}
}
