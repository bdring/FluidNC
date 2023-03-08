#pragma once

// Kinematics interface.
#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"
#include "../MotionControl.h"
#include "../Planner.h"
#include "../Types.h"
#include "src/Machine/Homing.h"

/*
Special types

You can add your own type of kinematics by adding 2 new files to the Kinematics folder.
my_delta.h
my_delta.cpp

Use some of the others as an example. Be sure to have the code for the config file.

Surround all the code in both files with
#ifdef my_delta_kinematics

#endif

Add a #define to this file for your kinematic
#define my_delta_kinematics

You will be ablr to add your kinematic using the config file.

*/

namespace Kinematics {
    class KinematicSystem;

    class Kinematics : public Configuration::Configurable {
    public:
        Kinematics() {}
        ~Kinematics();

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        void init();
        void init_position();
        void config_kinematics();

        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position);
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis);
        void transform_cartesian_to_motors(float* motors, float* cartesian);

        bool canHome(AxisMask axisMask);
        void releaseMotors(AxisMask axisMask, MotorMask motors);
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited);

    private:
        ::Kinematics::KinematicSystem* _system = nullptr;
    };

    class KinematicSystem : public Configuration::Configurable {
    public:
        KinematicSystem() = default;

        KinematicSystem(const KinematicSystem&)            = delete;
        KinematicSystem(KinematicSystem&&)                 = delete;
        KinematicSystem& operator=(const KinematicSystem&) = delete;
        KinematicSystem& operator=(KinematicSystem&&)      = delete;

        // Kinematic system interface.
        virtual bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) = 0;
        virtual void init()                                                                         = 0;
        virtual void init_position()                                                  = 0;  // used to set the machine position at init
        virtual void motors_to_cartesian(float* cartesian, float* motors, int n_axis) = 0;

        virtual void transform_cartesian_to_motors(float* motors, float* cartesian) = 0;

        virtual bool canHome(AxisMask axisMask) { return false; }
        virtual void releaseMotors(AxisMask axisMask, MotorMask motors) {}
        virtual bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) { return false; }

        // Configuration interface.
        void afterParse() override {}
        void group(Configuration::HandlerBase& handler) override {}
        void validate() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        virtual const char* name() const = 0;

        // Virtual base classes require a virtual destructor.
        virtual ~KinematicSystem() {}
    };

    using KinematicsFactory = Configuration::GenericFactory<KinematicSystem>;
};
