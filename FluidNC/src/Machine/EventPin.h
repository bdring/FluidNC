#pragma once

#include "Event.h"
#include "InputPin.h"
#include "Alarm.h"
#include <string>

class EventPin : public InputPin {
protected:
    const Event* _event;
    ExecAlarm    _alarm;

public:
    EventPin(const Event* event, const ExecAlarm alarm, const char* legend, char letter = '\0') :
        InputPin(legend, letter), _event(event), _alarm(alarm) {};

    void trigger(bool active) override;

    ~EventPin() {}
};
