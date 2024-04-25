#pragma once

#include "EventPin.h"
#include "../Pin.h"
namespace Machine {
    class ProbeEventPin : public EventPin {
    private:
        bool _value = 0;
        Pin* _pin = nullptr;

    public:
        ProbeEventPin(const char* legend, Pin& pin);

        void init();
        void update(bool state) override;
        bool get();
    };
}
