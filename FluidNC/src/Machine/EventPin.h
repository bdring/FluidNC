#pragma once

#include "src/Event.h"
#include <string>

class EventPin {
protected:
    const Event* _event;  
    std::string  _legend;  // The name that appears in init() messages and the name of the configuration item

public:

    EventPin(const Event* event, const char* legend) : _event(event), _legend(legend) {};

    virtual void update(bool state) {};

    virtual void trigger(bool active);

    const std::string& legend() { return _legend; }

    ~EventPin() {}
};
