#include "Axes.h"
#include "Axis.h"

#include <cstring>

namespace Machine {
    void Axis::group(Configuration::HandlerBase& handler) {
        handler.item("steps_per_mm", _stepsPerMm);
        handler.item("max_rate", _maxRate);
        handler.item("acceleration", _acceleration);
        handler.item("max_travel", _maxTravel);
        handler.item("soft_limits", _softLimits);
        handler.section("homing", _homing);

        char tmp[7];
        tmp[0] = 0;
        strcat(tmp, "motor");

        for (size_t g = 0; g < MAX_NUMBER_GANGED; ++g) {
            tmp[5] = char(g + '0');
            tmp[6] = '\0';
            handler.section(tmp, _motors[g], _axis, g);
        }
    }

    void Axis::afterParse() {
        for (size_t i = 0; i < MAX_NUMBER_GANGED; ++i) {
            if (_motors[i] == nullptr) {
                _motors[i] = new Motor(_axis, i);
            }
        }
    }

    void Axis::init() {
        for (uint8_t motor_index = 0; motor_index < Axis::MAX_NUMBER_GANGED; motor_index++) {
            _motors[motor_index]->init();
        }
        if (_homing) {
            _homing->init();
            set_bitnum(Axes::homingMask, _axis);
        }
    }

    // Checks if a motor matches this axis:
    bool Axis::hasMotor(const MotorDrivers::MotorDriver* const motor) const {
        for (uint8_t motor_index = 0; motor_index < MAX_NUMBER_GANGED; motor_index++) {
            if (_motors[motor_index]->_motor == motor) {
                return true;
            }
        }
        return false;
    }

    Axis::~Axis() {
        for (uint8_t motor_index = 0; motor_index < MAX_NUMBER_GANGED; motor_index++) {
            delete _motors[motor_index];
        }
    }
}
