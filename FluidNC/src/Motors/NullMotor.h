#pragma once

#include "MotorDriver.h"

namespace MotorDrivers {
    class Nullmotor : public MotorDriver {
    public:
        Nullmotor() = default;

        bool set_homing_mode(bool isHoming) { return false; }

        bool isReal() override { return false; }

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {}

        const char* name() const override { return "null_motor"; }
    };
}
