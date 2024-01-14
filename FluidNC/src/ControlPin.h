#pragma once

#include "Pin.h"
#include "Machine/EventPin.h"
namespace Machine {
    class ControlPin : public EventPin {
    private:
        const char _letter;  // The name that appears in init() messages and the name of the configuration item

    public:
        ControlPin(Event* event, const char* legend, char letter) : EventPin(event, legend), _letter(letter) {}

        void init();

        Pin _pin;

        bool get() { return _pin.read(); }

        char letter() { return _letter; };

        ~ControlPin();
    };
}
