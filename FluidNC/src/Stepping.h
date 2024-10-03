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
        static const int ticksPerMicrosecond = fStepperTimer / 1000000;

        static bool    _switchedStepper;
        static int32_t _stepPulseEndTime;


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

        static size_t _segments;

        static uint32_t _idleMsecs;
        static uint32_t _pulseUsecs;
        static uint32_t _directionDelayUsecs;
        static uint32_t _disableDelayUsecs;

        static int _engine;

        // Interfaces to stepping engine
        static void init();

        static void assignMotor(int axis, int motor, int step_pin, bool step_invert, int dir_pin, bool dir_invert);

        static void setStepPin(int axis, int motor, int pin, bool invert);
        static void setDirPin(int axis, int motor, int pin, bool invert);

        static void reset();  // Clean up old state and start fresh
        static void beginLowLatency();
        static void endLowLatency();
        static void startPulseTimer();
        static void waitPulse();      // Wait for pulse length
        static void waitDirection();  // Wait for direction delay
        static void waitMotion();     // Wait for motion to complete
        static void finishPulse();    // Cleanup after unstep


        static uint32_t maxPulsesPerSec();

        // Timers
        static void setTimerPeriod(uint16_t timerTicks);
        static void startTimer();
        static void stopTimer();

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
    };
}
extern const EnumItem stepTypes[];
