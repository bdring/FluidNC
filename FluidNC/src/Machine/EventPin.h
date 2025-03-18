#pragma once

#include "src/Event.h"
#include "src/Pin.h"
#include <string>

class EventPin {
protected:
    const Event* _event;
    std::string  _legend;  // The name that appears in init() messages and the name of the configuration item
    bool         _value = false;
    Pin          _pin;

public:
    EventPin(const Event* event, const char* legend) : _event(event), _legend(legend) {};

    void init();

    bool defined() { return _pin.defined(); }

    void update(bool state) { _value = state; };
    bool get() { return _value; }

    virtual void trigger(bool active);

    Pin& pin() { return _pin; }

    const std::string& legend() { return _legend; }

    ~EventPin() {}
};

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
