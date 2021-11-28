// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"
#include "../Assert.h"
#include "../Configuration/GenericFactory.h"
#include "../Configuration/HandlerBase.h"
#include "../Configuration/Configurable.h"

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

#include "../Configuration/Configurable.h"

#include <cstdint>

namespace MotorDrivers {
    class MotorDriver : public Configuration::Configurable {
    public:
        MotorDriver() = default;

        static constexpr int      max_n_axis = MAX_N_AXIS;
        static constexpr uint32_t axis_mask  = (1 << max_n_axis) - 1;

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

        // read_settings(), called from init(), re-establishes the motor
        // setup from configurable arameters.
        // TODO Architecture: Maybe this should be subsumed by init()
        virtual void read_settings() {}

        // set_homing_mode() is called from motors_set_homing_mode(),
        // which in turn is called at the beginning of a homing cycle
        // with isHoming true, and at the end with isHoming false.
        // Some motor types require differ setups for homing and
        // normal operation.  Returns true if the motor can home
        virtual bool set_homing_mode(bool isHoming) = 0;

        // set_disable() disables or enables a motor.  It is used to
        // make a motor transition between idle and non-idle states.
        virtual void set_disable(bool disable);

        // set_direction() sets the motor movement direction.  It is
        // invoked for every motion segment.
        virtual void set_direction(bool);

        // step() initiates a step operation on a motor.  It is called
        // from motors_step() for ever motor than needs to step now.
        // For ordinary step/direction motors, it sets the step pin
        // to the active state.
        virtual void step();

        // unstep() turns off the step pin, if applicable, for a motor.
        // It is called from motors_unstep() for all motors, since
        // motors_unstep() is used in many contexts where the previous
        // states of the step pins are unknown.
        virtual void unstep();

        // this is used to configure and test motors. This would be used for Trinamic
        virtual void config_motor() {}

        // test(), called from init(), checks to see if a motor is
        // responsive, returning true on failure.  Typical
        // implementations also display messages to show the result.
        // TODO Architecture: Should this be private?
        virtual bool test();

        // update() is used for some types of "smart" motors that
        // can be told to move to a specific position.  It is
        // called from a periodic task.
        virtual void update() {}

        // Name is required for the configuration factory to work.
        virtual const char* name() const = 0;

        // Test for a real motor as opposed to a NullMotor placeholder
        virtual bool isReal() { return true; }

        // Virtual base classes require a virtual destructor.
        virtual ~MotorDriver() {}

    protected:
        String axisName() const;

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
        size_t axis_index() const;       // X_AXIS, etc
        size_t dual_axis_index() const;  // motor number 0 or 1
    };

    using MotorFactory = Configuration::GenericFactory<MotorDriver>;
}
