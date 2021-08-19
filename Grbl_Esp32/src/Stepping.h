#pragma once

/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn, Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

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
        uint32_t _pulseUsecs          = 3;
        uint32_t _directionDelayUsecs = 0;
        uint32_t _disableDelayUsecs   = 0;

        int _engine = RMT;

        // Interfaces to stepping engine
        void init();

        void synchronize();  // Wait for motion to complete
        void reset();        // Clean up old state and start fresh
        void beginLowLatency();
        void endLowLatency();
        void startPulseTimer();
        void waitPulse();      // Wait for pulse length
        void waitDirection();  // Wait for direction delay
        void waitMotion();     // Wait for motion to complete
        void finishPulse();    // Cleanup after unstep

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
