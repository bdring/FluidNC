#pragma once

#include "src/Event.h"
#include <string>

class EventPin {
protected:
    Event* _event = nullptr;  // Subordinate event that is called conditionally

public:
    std::string _legend;  // The name that appears in init() messages and the name of the configuration item

    EventPin(Event* event, const char* legend);

    virtual void update(bool state) {};

    void trigger(bool active);

    ~EventPin() {}
};
