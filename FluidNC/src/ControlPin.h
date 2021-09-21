#pragma once

#include "Pin.h"

class ControlPin {
private:
    bool           _value;
    const char     _letter;
    volatile bool& _rtVariable;
    const char*    _legend;

    void handleISR();

public:
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
