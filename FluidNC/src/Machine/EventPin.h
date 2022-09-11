#pragma once

#include "src/Pin.h"
#include "src/Event.h"
#include "src/Logging.h"

#include <freertos/timers.h>  // TimerHandle_t
#include <list>

namespace Machine {
    class EventPin : public Event {
    private:
        static std::list<EventPin*> _blockedPins;

    protected:
        void handleISR();
        CreateISRHandlerFor(EventPin, handleISR);

        Event* _event = nullptr;  // Subordinate event that is called conditionally

        pinnum_t _gpio;

        static bool inactive(EventPin* pin);

        static void eventPinTimerCallback(void*);

    public:
        static TimerHandle_t _eventPinTimer;

        String _legend;  // The name that appears in init() messages and the name of the configuration item

        EventPin(Event* event, const char* legend, Pin* pin);

        // This is a pointer instead of a reference because the derived classes
        // like ControlPin and LimitPin "own" the actual Pin object.  That is
        // necessary because those objects are configurable and must stay
        // within their class for later operations on the configuration tree.
        Pin* _pin = nullptr;

        void init();
        bool get();
        void run(void* arg) override;
        void block();

        static void startTimer();
        static void check();

        virtual void update(bool state) {};

        // After the ISR triggers it is turned off until reArm() is called,
        // thus preventing unwanted retriggering due to switch noise or held buttons
        virtual void reArm();

        ~EventPin();
    };
};
