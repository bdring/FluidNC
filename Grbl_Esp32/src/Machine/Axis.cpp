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

        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; ++i) {
            tmp[5] = char(i + '0');
            tmp[6] = '\0';
            handler.section(tmp, _motors[i], _axis, i);
        }
    }

    void Axis::afterParse() {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; ++i) {
            if (_motors[i] == nullptr) {
                _motors[i] = new Motor(_axis, i);
            }
        }
    }

    void Axis::init() {
        for (uint8_t i = 0; i < Axis::MAX_MOTORS_PER_AXIS; i++) {
            _motors[i]->init();
        }
        if (_homing) {
            _homing->init();
            set_bitnum(Axes::homingMask, _axis);
        }
    }

    // Checks if a motor matches this axis:
    bool Axis::hasMotor(const MotorDrivers::MotorDriver* const driver) const {
        for (uint8_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            if (_motors[i]->_driver == driver) {
                return true;
            }
        }
        return false;
    }

    Axis::~Axis() {
        for (uint8_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            delete _motors[i];
        }
    }
}
