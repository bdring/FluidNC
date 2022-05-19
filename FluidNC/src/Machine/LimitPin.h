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
        bool& _pHardLimits;

        // _pHardLimits is a reference to the _limited member of
        // the Motor class.  Setting it when the Limit ISR fires
        // lets the motor driver respond rapidly to a limit switch
        // touch, increasing the accuracy of homing
        volatile bool& _pLimited;
        volatile bool* _pExtraLimited = nullptr;

        volatile uint32_t* _posLimits = nullptr;
        volatile uint32_t* _negLimits = nullptr;

        void IRAM_ATTR handleISR();

        CreateISRHandlerFor(LimitPin, handleISR);

        void read();

    public:
        LimitPin(Pin& pin, int axis, int motorNum, int direction, bool& phardLimits, bool& pLimited);

        Pin& _pin;

        String _legend;

        void init();
        bool get() { return _value; }
        void makeDualMask();  // makes this a mask for motor0 and motor1
        void setExtraMotorLimit(int axis, int motorNum);

        ~LimitPin();
    };
}
