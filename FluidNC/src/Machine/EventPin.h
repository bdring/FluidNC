#pragma once

#include "Event.h"
#include "InputPin.h"
#include <string>

class EventPin : public InputPin {
protected:
    const Event* _event;

public:
    EventPin(const Event* event, const char* legend) : InputPin(legend), _event(event) {};

    void trigger(bool active) override;

    ~EventPin() {}
};
