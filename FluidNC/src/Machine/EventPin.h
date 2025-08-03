#pragma once

#include "src/Event.h"
#include "src/Pin.h"
#include <string>

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

    const std::string& legend() { return _legend; }

    ~InputPin() {}
};

class EventPin : public InputPin {
protected:
    const Event* _event;

public:
    EventPin(const Event* event, const char* legend) : InputPin(legend), _event(event) {};

    void trigger(bool active) override;

    ~EventPin() {}
};
