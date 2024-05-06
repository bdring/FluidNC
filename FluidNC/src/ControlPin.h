#pragma once

#include "Pin.h"
#include "Machine/EventPin.h"
namespace Machine {
    class ControlPin : public EventPin {
    private:
        char _letter;  // The name that appears in init() messages and the name of the configuration item
        Pin  _pin;

    public:
        ControlPin(const Event* event, const char* legend, char letter) : EventPin(event, legend), _letter(letter) {}

        void init();

        bool get() { return _pin.read(); }

        Pin& pin() { return _pin; }

        char letter() { return _letter; };

        ~ControlPin();
    };
}
