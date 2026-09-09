#pragma once

#include "Machine/EventPin.h"
namespace Machine {
    class ControlPin : public EventPin {
    public:
        ControlPin(const Event* event, const char* legend, char letter) :
            EventPin(event, ExecAlarm::StartupPin, legend, letter) {}

        ~ControlPin();
    };
}
