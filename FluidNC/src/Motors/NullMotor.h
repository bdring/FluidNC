#pragma once

#include "MotorDriver.h"

namespace MotorDrivers {
    class Nullmotor : public MotorDriver {
    public:
        Nullmotor(const char* name) : MotorDriver(name) {}

        bool set_homing_mode(bool isHoming) { return false; }

        bool isReal() override { return false; }

        bool can_self_home() override { return false; }

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {}
    };
}
