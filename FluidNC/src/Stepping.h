// Copyright (c) 2021 Stefan de Bruijn
// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include <esp32-hal-timer.h>  // hw_timer_t

namespace Machine {
    class Stepping : public Configuration::Configurable {
    public:
        // fStepperTimer should be an integer divisor of the bus speed, i.e. of fTimers
        static const uint32_t fStepperTimer = 20000000;  // frequency of step pulse timer

    private:
        static const int   stepTimerNumber = 0;
        static hw_timer_t* stepTimer;
        static void        onStepperDriverTimer();

        static const uint32_t fTimers             = 80000000;  // the frequency of ESP32 timers
        static const int      ticksPerMicrosecond = fStepperTimer / 1000000;

        bool    _switchedStepper = false;
        int32_t _stepPulseEndTime;

    public:
        // Counts stepper ISR invocations.  This variable can be inspected
        // from the mainline code to determine if the stepper ISR is running,
        // since printing from the ISR is not a good idea.
        static uint32_t isr_count;

        enum stepper_id_t {
            TIMED = 0,
            RMT,
            I2S_STATIC,
            I2S_STREAM,
        };

        Stepping() = default;

        uint8_t  _idleMsecs           = 255;
        uint32_t _pulseUsecs          = 4;
        uint32_t _directionDelayUsecs = 0;
        uint32_t _disableDelayUsecs   = 0;

        int _engine = RMT;

        // Interfaces to stepping engine
        void init();

        void reset();  // Clean up old state and start fresh
        void beginLowLatency();
        void endLowLatency();
        void startPulseTimer();
        void waitPulse();      // Wait for pulse length
        void waitDirection();  // Wait for direction delay
        void waitMotion();     // Wait for motion to complete
        void finishPulse();    // Cleanup after unstep

        uint32_t maxPulsesPerSec();

        // Timers
        void setTimerPeriod(uint16_t timerTicks);
        void startTimer();
        void stopTimer();

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
    };
}
extern EnumItem stepTypes[];
