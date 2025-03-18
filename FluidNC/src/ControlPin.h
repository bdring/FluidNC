#pragma once

#include "Machine/EventPin.h"
namespace Machine {
    class ControlPin : public EventPin {
    private:
        char _letter;  // The name that appears in init() messages and the name of the configuration item
    public:
        ControlPin(const Event* event, const char* legend, char letter) : EventPin(event, legend), _letter(letter) {}

        char letter() { return _letter; };

        ~ControlPin();
    };
}
