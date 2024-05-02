// Copyright (c) 2021 Stefan de Bruijn
// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "Driver/StepTimer.h"

namespace Machine {
    class Stepping : public Configuration::Configurable {
    public:
        // fStepperTimer should be an integer divisor of the bus speed, i.e. of fTimers
        static const uint32_t fStepperTimer = 20000000;  // frequency of step pulse timer

    private:
        static bool onStepperDriverTimer();

        static const int ticksPerMicrosecond = fStepperTimer / 1000000;

        bool    _switchedStepper = false;
        int32_t _stepPulseEndTime;

    public:
        enum stepper_id_t {
            TIMED = 0,
            RMT,
            I2S_STATIC,
            I2S_STREAM,
        };

        Stepping() = default;

        // _segments is the number of entries in the step segment buffer between the step execution algorithm
        // and the planner blocks. Each segment is set of steps executed at a constant velocity over a
        // fixed time defined by ACCELERATION_TICKS_PER_SECOND. They are computed such that the planner
        // block velocity profile is traced exactly. The size of this buffer governs how much step
        // execution lead time there is for other processes to run.  The latency for a feedhold or other
        // override is roughly 10 ms times _segments.

        size_t _segments = 12;

        uint32_t _idleMsecs           = 255;
        uint32_t _pulseUsecs          = 4;
        uint32_t _directionDelayUsecs = 0;
        uint32_t _disableDelayUsecs   = 0;

        static int _engine;

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
        void        setTimerPeriod(uint16_t timerTicks);
        void        startTimer();
        static void stopTimer();

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
    };
}
extern const EnumItem stepTypes[];
