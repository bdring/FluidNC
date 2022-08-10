#pragma once

#include "Pin.h"
#include <esp_attr.h>  // IRAM_ATTR
#include "Event.h"

class ControlPin : public Event {
private:
    void IRAM_ATTR handleISR();
    CreateISRHandlerFor(ControlPin, handleISR);

    Event* _event = nullptr;

public:
    const char* _legend;  // The name that appears in init() messages and the name of the configuration item
    const char  _letter;  // The letter that appears in status reports when the pin is active

    ControlPin(Event* event, const char* legend, char letter) : _event(event), _letter(letter), _legend(legend) {}

    Pin _pin;

    void init();
    bool get() { return _pin.read(); }
    void run() override {
        // Since we do not trust the ISR to always trigger precisely,
        // we check the pin state before calling the event handler
        if (get()) {
            _event->run();
        }
    }

    ~ControlPin();
};
