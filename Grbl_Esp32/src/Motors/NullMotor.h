#pragma once

#include "MotorDriver.h"

namespace Motors {
    class Nullmotor : public Motor {
    public:
        Nullmotor() = default;

        bool set_homing_mode(bool isHoming) { return false; }

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {}

        const char* name() const override { return "null_motor"; }
    };
}
