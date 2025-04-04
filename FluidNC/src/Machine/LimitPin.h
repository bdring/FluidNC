#pragma once

#include "EventPin.h"

namespace Machine {
    class LimitPin : public EventPin {
    private:
        uint32_t _bitmask = 0;

        // _pHardLimits is a reference so the shared variable at the
        // Endstops level can be changed at runtime to control the
        // limit behavior dynamically.
        bool& _pHardLimits;

        // _pLimited is a reference to the _limited member of
        // the Motor class.  Setting it when the Limit ISR fires
        // lets the motor driver respond rapidly to a limit switch
        // touch, increasing the accuracy of homing
        // _pExtraLimited lets the limit control two motors, as with
        // CoreXY
        volatile bool* _pLimited;
        volatile bool* _pExtraLimited = nullptr;

        volatile uint32_t* _posLimits = nullptr;
        volatile uint32_t* _negLimits = nullptr;

    public:
        LimitPin(int axis, int motorNum, int direction, bool& phardLimits);

        void trigger(bool active) override;

        void makeDualMask();  // makes this a mask for motor0 and motor1
        void setExtraMotorLimit(int axis, int motorNum);

        bool isHard() { return _pHardLimits; }
        void init();

        int _axis;
        int _motorNum;
    };
}
