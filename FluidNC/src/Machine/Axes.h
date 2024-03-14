// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "Axis.h"
#include "../EnumItem.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Axes : public Configuration::Configurable {
        bool _switchedStepper = false;

    public:
        static constexpr const char* _names = "XYZABC";

        Axes();

        // Bitmasks to collect information about axes that have limits and homing
        static MotorMask posLimitMask;
        static MotorMask negLimitMask;
        static MotorMask limitMask;
        static MotorMask motorMask;

        static AxisMask homingMask;

        Pin _sharedStepperDisable;
        Pin _sharedStepperReset;

        inline char axisName(int index) { return index < MAX_N_AXIS ? _names[index] : '?'; }  // returns axis letter

        static inline size_t    motor_bit(size_t axis, size_t motor) { return motor ? axis + 16 : axis; }
        static inline AxisMask  motors_to_axes(MotorMask motors) { return (motors & 0xffff) | (motors >> 16); }
        static inline MotorMask axes_to_motors(AxisMask axes) { return axes | (axes << 16); }

        int   _numberAxis = 0;
        Axis* _axis[MAX_N_AXIS];

        // Some small helpers to find the axis index and axis motor number for a given motor. This
        // is helpful for some motors that need this info, as well as debug information.
        size_t findAxisIndex(const MotorDrivers::MotorDriver* const motor) const;
        size_t findAxisMotor(const MotorDrivers::MotorDriver* const motor) const;

        MotorMask hardLimitMask();

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

        void set_disable(int axis, bool disable);
        void set_disable(bool disable);
        void step(uint8_t step_mask, uint8_t dir_mask);
        void unstep();
        void config_motors();

        std::string maskToNames(AxisMask mask);

        bool namesToMask(const char* names, AxisMask& mask);

        std::string motorMaskToNames(MotorMask mask);

        // Configuration helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        ~Axes();
    };
}
extern EnumItem axisType[];
