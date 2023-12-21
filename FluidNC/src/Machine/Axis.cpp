#include "Axes.h"
#include "Axis.h"
#include "MachineConfig.h"  // config

#include <cstring>

namespace Machine {
    void Axis::group(Configuration::HandlerBase& handler) {
        handler.item("steps_per_mm", _stepsPerMm, 0.001, 100000.0);
        handler.item("max_rate_mm_per_min", _maxRate, 0.001, 100000.0);
        handler.item("acceleration_mm_per_sec2", _acceleration, 0.001, 100000.0);
        handler.item("max_travel_mm", _maxTravel, 0.1, 10000000.0);
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
        uint32_t stepRate = uint32_t(_stepsPerMm * _maxRate / 60.0);
        auto     maxRate  = config->_stepping->maxPulsesPerSec();
        Assert(stepRate <= maxRate, "Stepping rate %d steps/sec exceeds the maximum rate %d", stepRate, maxRate);
        if (_homing == nullptr) {
            _homing         = new Homing();
            _homing->_cycle = 0;
        }
        if (_motors[0] == nullptr) {
            _motors[0] = new Machine::Motor(_axis, 0);
        }
    }

    void Axis::init() {
        for (size_t i = 0; i < Axis::MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m) {
                log_info("  Motor" << i);
                m->init();
            }
        }
        if (_homing && _homing->_cycle != Machine::Homing::set_mpos_only) {
            _homing->init();
            set_bitnum(Axes::homingMask, _axis);
        }

        if (!_motors[0] && _motors[1]) {
            sys.state = State::ConfigAlarm;
            log_error("motor1 defined without motor0");
        }

        // If dual motors and only one motor has switches, this is the configuration
        // for a POG style squaring. The switch should report as being on both axes
        if (hasDualMotor() && (motorsWithSwitches() == 1)) {
            _motors[0]->makeDualSwitches();
            _motors[1]->makeDualSwitches();
        }
    }

    void Axis::config_motors() {
        for (int motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; ++motor) {
            auto mot = _motors[motor];
            if (mot)
                mot->config_motor();
        }
    }

    // Checks if a motor matches this axis:
    bool Axis::hasMotor(const MotorDrivers::MotorDriver* const driver) const {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m && m->_driver == driver) {
                return true;
            }
        }
        return false;
    }

    // Does this axis have 2 motors?
    bool Axis::hasDualMotor() { return _motors[0] && _motors[0]->isReal() && _motors[1] && _motors[1]->isReal(); }

    // How many motors have switches defined?
    int Axis::motorsWithSwitches() {
        int count = 0;
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m && m->hasSwitches()) {
                count++;
            }
        }
        return count;
    }

    float Axis::commonPulloff() {
        auto motor0Pulloff = _motors[0]->_pulloff;
        if (hasDualMotor()) {
            auto motor1Pulloff = _motors[1]->_pulloff;
            return std::min(motor0Pulloff, motor1Pulloff);
        } else {
            return motor0Pulloff;
        }
    }

    // returns the offset between the pulloffs
    // value is positive when motor1 has a larger pulloff
    float Axis::extraPulloff() {
        if (hasDualMotor()) {
            return _motors[1]->_pulloff - _motors[0]->_pulloff;
        } else {
            return 0.0f;
        }
    }

    Axis::~Axis() {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            if (_motors[i]) {
                delete _motors[i];
            }
        }
    }
}
