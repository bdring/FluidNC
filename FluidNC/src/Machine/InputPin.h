#pragma once

#include "Pin.h"
#include <string>

// InputPin is used for user digital inputs and as a base class for EventPin

class InputPin : public Pin {
protected:
    std::string _legend;  // The name that appears in init() messages and the name of the configuration item
    bool        _value = false;

public:
    InputPin(const char* legend) : _legend(legend) {};

    void init();

    void update(bool state) { _value = state; };
    bool get() { return _value; }

    virtual void trigger(bool active);

    // legend() is the functional usage of the pin, for example "digital0_pin",
    // whereas name() is the physical pin assigned to that function, for example "gpio.15"
    const char* legend() { return _legend.c_str(); }

    ~InputPin() {}
};
