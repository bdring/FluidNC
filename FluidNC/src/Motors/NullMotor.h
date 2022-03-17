#pragma once

#include "MotorDriver.h"

#include <cstdint>

namespace MotorDrivers {
    class Nullmotor : public MotorDriver {
        int32_t direction = 0;
        int32_t steps     = 0;
        int     state     = 0;
        bool    disabled  = true;

    public:
        Nullmotor() = default;

        bool set_homing_mode(bool isHoming) { return false; }

        bool isReal() override { return false; }

        void step() override {
            Assert(!disabled, "Cannot step motor while disabled.");
            if (state == 0) {
                state = 1;
                steps += direction;
            }
        }

        void unstep() override {
            state = 0;
        }

        void set_direction(bool dir) override {
            Assert(!disabled, "Cannot set direction while disabled.");
            direction = (dir ? -1 : 1);
        }

        void set_disable(bool disable) override { disabled = disable; }

        void debug_message(Print& out) override;

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {}

        const char* name() const override { return "null_motor"; }
    };
}
