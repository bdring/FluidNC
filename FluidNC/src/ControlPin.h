#pragma once

#include "Pin.h"
#include <esp_attr.h>  // IRAM_ATTR
#include "Machine/EventPin.h"
namespace Machine {
    class ControlPin : public Machine::EventPin {
    private:
        const char _letter;  // The name that appears in init() messages and the name of the configuration item

    public:
        ControlPin(Event* event, const char* legend, char letter) : EventPin(event, legend), _letter(letter) {}

        char letter() { return _letter; };

        void run(void* arg) override;

        ~ControlPin();
    };
}
