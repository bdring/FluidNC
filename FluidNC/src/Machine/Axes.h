// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "Axis.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Axes : public Configuration::Configurable {
        bool _switchedStepper = false;

        // During homing, this is used to stop stepping on motors that have
        // reached their limit switches, by clearing bits in the mask.
        MotorMask _motorLockoutMask = 0;

    public:
        static constexpr const char* _names = "XYZABC";

        Axes();

        // Bitmasks to collect information about axes that have limits and homing
        static MotorMask posLimitMask;
        static MotorMask negLimitMask;
        static MotorMask homingMask;
        static MotorMask limitMask;
        static MotorMask motorMask;

        inline char axisName(int index) { return index < MAX_N_AXIS ? _names[index] : '?'; }  // returns axis letter

        Pin _sharedStepperDisable;

        int   _numberAxis = 0;
        Axis* _axis[MAX_N_AXIS];

        // Some small helpers to find the axis index and axis motor number for a given motor. This
        // is helpful for some motors that need this info, as well as debug information.
        size_t findAxisIndex(const MotorDrivers::MotorDriver* const motor) const;
        size_t findAxisMotor(const MotorDrivers::MotorDriver* const motor) const;

        inline bool hasHardLimits() const {
            for (int axis = 0; axis < _numberAxis; ++axis) {
                auto a = _axis[axis];

                for (int motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; ++motor) {
                    auto m = a->_motors[motor];
                    if (m && m->_hardLimits) {
                        return true;
                    }
                }
            }
            return false;
        }

        void init();

        // These are used during homing cycles.
        // The return value is a bitmask of axes that can home
        MotorMask set_homing_mode(AxisMask homing_mask, bool isHoming);
        void      unlock_all_motors();
        void      lock_motors(MotorMask motor_mask);
        void      unlock_motors(MotorMask motor_mask);

        void set_disable(int axis, bool disable);
        void set_disable(bool disable);
        void step(uint8_t step_mask, uint8_t dir_mask);
        void unstep();
        void config_motors();

        // Configuration helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        ~Axes();
    };
}
