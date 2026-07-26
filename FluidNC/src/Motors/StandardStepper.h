#pragma once

#include "MotorDriver.h"

namespace MotorDrivers {
    class StandardStepper : public MotorDriver {
    public:
        //StandardStepper(axis_t axis_index, Pin step_pin, Pin dir_pin, Pin disable_pin);

        StandardStepper(const char* name) : MotorDriver(name) {}

        // Overrides for inherited methods
        void init() override;

        // No special action, but return true to say homing is possible
        bool set_homing_mode(bool isHoming) override { return true; }
        bool can_self_home() override { return false; }
        void set_disable(bool) override;

        void init_step_dir_pins();

    protected:
        void config_message() override;

        Pin _step_pin;
        Pin _dir_pin;
        Pin _disable_pin;

        // Configuration handlers:
        void validate() override;

        void group(Configuration::HandlerBase& handler) override {
            // These 3 fields are the base field set every other motor driver type that
            // derives from StandardStepper (stepstick, tmc_*, etc.) inherits by calling
            // StandardStepper::group() first -- annotated once here, not repeated in
            // each derived driver's own group().

            // @config step_pin
            // @default NO_PIN
            // Step pulse output to the driver.
            handler.item("step_pin", _step_pin);

            // @config direction_pin
            // @default NO_PIN
            // Direction output to the driver.
            handler.item("direction_pin", _dir_pin);

            // @config disable_pin
            // @default NO_PIN
            // Enable/disable output to the driver (active state depends on the driver
            // hardware -- invert with the pin's :low attribute if needed).
            handler.item("disable_pin", _disable_pin);
        }
    };
}
