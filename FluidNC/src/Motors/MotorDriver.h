// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#include "Configuration/GenericFactory.h"
#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"

/*
    Header file for Motor Classes
    Here is the hierarchy
        Motor
            Nullmotor
            StandardStepper
                TrinamicDriver
            Unipolar
            RC Servo

    See motorClass.cpp for more details
*/

#include "Configuration/Configurable.h"

#include <cstdint>

namespace MotorDrivers {
    class MotorDriver : public Configuration::Configurable {
        const char* _name;

    public:
        MotorDriver(const char* name) : _name(name) {}

        static constexpr int      max_n_axis = MAX_N_AXIS;
        static constexpr AxisMask axis_mask  = (1 << max_n_axis) - 1;

        // init() establishes configured motor parameters.  It is called after
        // all motor objects have been constructed.
        virtual void init() {}

        // debug_message() displays motor-specific information that can be
        // used to assist with motor configuration.  For many motor types,
        // it is a no-op.
        // TODO Architecture: Should this be private?  It only applies to
        // Trinamic drivers so maybe there is a cleaner approach to solving
        // the stallguard debugging problem.
        virtual void debug_message();

        // set_homing_mode() is called from motors_set_homing_mode(),
        // which in turn is called at the beginning of a homing cycle
        // with isHoming true, and at the end with isHoming false.
        // Some motor types require differ setups for homing and
        // normal operation.  Returns true if the motor can home
        virtual bool set_homing_mode(bool isHoming) = 0;

        // this is used to determine if the motor can home
        // it is tested when hoing cycles are requested.
        virtual bool can_self_home() = 0;

        // set_disable() disables or enables a motor.  It is used to
        // make a motor transition between idle and non-idle states.
        virtual void set_disable(bool disable);

        // this is used to configure and test motors. This would be used for Trinamic
        virtual void config_motor() {}

        // test(), called from init(), checks to see if a motor is
        // responsive, returning true on failure.  Typical
        // implementations also display messages to show the result.
        // TODO Architecture: Should this be private?
        virtual bool test();

        // Name is required for the configuration factory to work.
        const char* name() { return _name; }

        // Test for a real motor as opposed to a NullMotor placeholder
        virtual bool isReal() { return true; }

        // Virtual base classes require a virtual destructor.
        virtual ~MotorDriver() {}

    protected:
        std::string axisName() const;

        // config_message(), called from init(), displays a message describing
        // the motor configuration - pins and other motor-specific items
        virtual void config_message() {}

        // _axis_index is the axis from XYZABC, while
        // _dual_axis_index is 0 for the first motor on that
        // axis and 1 for the second motor.
        // These variables are used for several purposes:
        // * Displaying the axis name in messages
        // * When reading settings, determining which setting
        //   applies to this motor
        // * For some motor types, it is necessary to maintain
        //   tables of all the motors of that type; those
        //   tables can be indexed by these variables.
        // TODO Architecture: It might be useful to cache a
        // reference to the axis settings entry.
        axis_t axis_index() const;       // X_AXIS, etc
        motor_t dual_axis_index() const;  // motor number 0 or 1
    };

    using MotorFactory = Configuration::GenericFactory<MotorDriver>;
}
