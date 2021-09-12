#pragma once

#include "RcServo.h"

namespace MotorDrivers {
    class Solenoid : public RcServo {
    public:
        Solenoid();
        Solenoid(size_t axis_index, gpio_num_t pwm_pin, float transition_poiont);
        void           set_location();
        void           update() override;
        void           init() override;
        void IRAM_ATTR set_disable(bool disable) override;

        float _transition_point;

    protected:
        void config_message() override;
    };
}
