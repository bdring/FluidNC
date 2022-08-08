#pragma once

#include "Pin.h"
#include <esp_attr.h>  // IRAM_ATTR

class ControlPin {
private:
    bool           _value;
    volatile bool& _rtVariable;  // The variable that is set when the pin is asserted
    bool           _attached    = false;
    int32_t        _debounceEnd = 0;

    // Interval during which we ignore repeated control pin activations
    const int debounceUs = 10000;  // 10000 us = 10 ms
    void IRAM_ATTR handleISR();
    CreateISRHandlerFor(ControlPin, handleISR);

public:
    const char* _legend;  // The name that appears in init() messages and the name of the configuration item
    const char  _letter;  // The letter that appears in status reports when the pin is active

    ControlPin(volatile bool& rtVariable, const char* legend, char letter) :
        _value(false), _letter(letter), _rtVariable(rtVariable), _legend(legend) {
        _rtVariable = _value;
    }

    Pin _pin;

    void init();
    bool get() { return _value; }

    String report();

    ~ControlPin();
};
