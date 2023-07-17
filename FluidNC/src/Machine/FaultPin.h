#pragma once

#include "EventPin.h"

namespace Machine {
    class FaultPin : public EventPin {
    public:
        FaultPin(Pin& pin);
    };
}
