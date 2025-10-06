// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "Axis.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Axes : public Configuration::Configurable {
        bool _switchedStepper = false;

    public:
        static const char* _axisNames[];

        //        static constexpr const char* _names = "XYZABC";

        Axes();

        // Bitmasks to collect information about axes that have limits and homing
        static MotorMask posLimitMask;
        static MotorMask negLimitMask;
        static MotorMask limitMask;
        static MotorMask motorMask;

        static AxisMask homingMask;

        static bool disabled;

        static Pin _sharedStepperDisable;
        static Pin _sharedStepperReset;

        static uint32_t _homing_runs;  // Number of Approach/Pulloff cycles

        static axis_t axisNum(std::string_view axis_name);

        static inline const char* axisName(axis_t axis) {  // returns axis letter as C string
            return axis < MAX_N_AXIS ? _axisNames[axis] : "?";
        }

        static inline size_t    motor_bit(axis_t axis, motor_t motor) { return motor ? size_t(axis) + 16 : size_t(axis); }
        static inline AxisMask  motors_to_axes(MotorMask motors) { return (motors & 0xffff) | (motors >> 16); }
        static inline MotorMask axes_to_motors(AxisMask axes) { return axes | (axes << 16); }

        static axis_t _numberAxis;
        static Axis*  _axis[MAX_N_AXIS];

        // Some small helpers to find the axis index and axis motor number for a given motor. This
        // is helpful for some motors that need this info, as well as debug information.
        static axis_t  findAxisIndex(const MotorDrivers::MotorDriver* const motor);
        static motor_t findAxisMotor(const MotorDrivers::MotorDriver* const motor);

        static MotorMask hardLimitMask();

        inline bool hasHardLimits() const {
            for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
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

        static void init();

        // These are used during homing cycles.
        // The return value is a bitmask of axes that can home
        static MotorMask set_homing_mode(AxisMask homing_mask, bool isHoming);

        static void set_disable(axis_t axis, bool disable);
        static void set_disable(bool disable);
        static void config_motors();

        static std::string maskToNames(AxisMask mask);

        static bool namesToMask(const char* names, AxisMask& mask);

        static std::string motorMaskToNames(MotorMask mask);

        // Configuration helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        ~Axes();
    };
}
