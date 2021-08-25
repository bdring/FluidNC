#pragma once

#include "../Pin.h"

#include <esp_attr.h>  // IRAM_ATTR

namespace Machine {
    class LimitPin {
    private:
        int _axis;
        int _motorNum;

        bool     _value   = 0;
        uint32_t _bitmask = 0;

        // _pHardLimits is a reference so the shared variable at the
        // Endstops level can be changed at runtime to control the
        // limit behavior dynamically.
        bool&              _pHardLimits;
        volatile uint32_t* _posLimits = nullptr;
        volatile uint32_t* _negLimits = nullptr;

        void IRAM_ATTR handleISR();

    public:
        LimitPin(Pin& pin, int axis, int motorNum, int direction, bool& phardLimits);

        Pin& _pin;

        String _legend;

        void init();
        bool get() { return _value; }
        void makeDualMask(); // makes this a mask for motor0 and motor1

        ~LimitPin();
    };
}
