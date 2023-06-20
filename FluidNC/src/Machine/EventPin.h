#pragma once

#include "../Pin.h"
#include "../Event.h"
#include "../Config.h"

namespace Machine {
    class EventPin {
    protected:
        static void gpioAction(void*, bool);

        Event* _event = nullptr;  // Subordinate event that is called conditionally

        pinnum_t _gpio;

    public:
        std::string _legend;  // The name that appears in init() messages and the name of the configuration item

        EventPin(Event* event, const char* legend, Pin* pin);

        // This is a pointer instead of a reference because the derived classes
        // like ControlPin and LimitPin "own" the actual Pin object.  That is
        // necessary because those objects are configurable and must stay
        // within their class for later operations on the configuration tree.
        Pin* _pin = nullptr;

        void init();
        bool get();

        virtual void update(bool state) {};

        virtual ~EventPin();
    };
};
