#include "Axes.h"
#include "Axis.h"
#include "MachineConfig.h"  // config

#include <cstring>

namespace Machine {
    void Axis::group(Configuration::HandlerBase& handler) {
        // @config steps_per_mm
        // @default 80.0
        // @tuning per-machine
        // Controller-side step-pulse resolution for this axis -- really "steps per GCode
        // unit," despite the name: in G21 (mm) mode, a one-unit move issues this many step
        // pulses; for a linear (XYZ) axis in G20 (inches) mode, the step count is instead
        // multiplied by 25.4. A rotary (ABC) axis always issues this many pulses per unit,
        // regardless of G20/G21. If using a microstepping driver, this already needs to
        // include the microstep multiplier.
        handler.item("steps_per_mm", _stepsPerMm, 0.001, 100000.0);

        // @config max_rate_mm_per_min
        // @default 1000.0
        // @tuning per-machine
        // Maximum feed rate (rapids and feed moves alike are capped here) for this axis.
        handler.item("max_rate_mm_per_min", _maxRate, 0.001, 250000.0);

        // @config acceleration_mm_per_sec2
        // @default 25.0
        // @tuning per-machine
        // Acceleration used for this axis's motion ramps.
        handler.item("acceleration_mm_per_sec2", _acceleration, 0.001, 100000.0);

        // @config max_travel_mm
        // @default 1000.0
        // @tuning per-machine
        // Working length of the axis, measured from its position immediately after homing
        // pull-off. If a second limit switch exists at the far end of travel (for hard
        // limits), make sure this value stays short enough that a soft-limit alarm trips
        // before that second switch would be physically reached.
        handler.item("max_travel_mm", _maxTravel, 0.1, 10000000.0);

        // @config soft_limits
        // @default false
        // @tuning typical
        // When true, a move that would exceed max_travel_mm is aborted before it starts;
        // jogs are instead constrained to stop at the travel limit rather than alarming.
        // Relies on accurate machine position, so the axis should be homed first -- always
        // home before jogging or running GCode when soft limits are enabled.
        handler.item("soft_limits", _softLimits);

        // @config idle_disable
        // @default true
        // Whether this axis participates in stepping.idle_ms auto-disable. Set to false to
        // keep this axis's motor(s) always enabled regardless of the global idle timeout --
        // e.g. a Z axis that would otherwise fall, or an RC servo axis that needs to stay
        // enabled to hold position.
        handler.item("idle_disable", _idleDisable);

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
        if (_motors[0] == nullptr) {
            _motors[0] = new Machine::Motor(_axis, 0);
        }
    }

    void Axis::init() {
        uint32_t stepRate = uint32_t(_stepsPerMm * _maxRate / 60.0);
        auto     maxRate  = Stepping::maxPulsesPerSec();
        Assert(stepRate <= maxRate, "Stepping rate %d steps/sec exceeds the maximum rate %d", stepRate, maxRate);

        for (size_t i = 0; i < Axis::MAX_MOTORS_PER_AXIS; i++) {
            auto m = _motors[i];
            if (m) {
                log_info("  Motor" << i);
                m->init();
            }
        }
        if (_homing && _homing->_cycle >= 0) {
            _homing->init();
            set_bitnum(Axes::homingMask, _axis);
        }

        if (!_motors[0] && _motors[1]) {
            log_config_error("motor1 defined without motor0");
        }

        // If dual motors and only one motor has switches, this is the configuration
        // for a POG style squaring. The switch should report as being on both axes
        if (hasDualMotor() && (motorsWithSwitches() == 1)) {
            _motors[0]->makeDualSwitches();
            _motors[1]->makeDualSwitches();
        }

        // see if the configured switches support the homing direction.
        if (_homing) {
            bool homing_dir_supported = false;
            auto direction            = _homing->_positiveDirection;
            for (motor_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
                auto m = _motors[i];
                if (m && m->supports_homing_dir(direction)) {
                    homing_dir_supported = true;
                    break;
                }
            }
            if (!homing_dir_supported) {
                log_warn("  Limit switches do not support " << (direction ? "positive" : "negative") << " homing dir");
            }
        }
    }

    void Axis::config_motors() {
        for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; ++motor) {
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
    bool Axis::hasDualMotor() {
        return _motors[0] && _motors[0]->isReal() && _motors[1] && _motors[1]->isReal();
    }

    // How many motors have switches defined?
    motor_t Axis::motorsWithSwitches() {
        motor_t count = 0;
        for (motor_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
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

    bool Axis::can_home() {
        for (motor_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            if (_motors[i]) {
                if (_motors[i]->can_home()) {
                    return true;
                }
            }
        }
        return false;
    }

    Axis::~Axis() {
        for (size_t i = 0; i < MAX_MOTORS_PER_AXIS; i++) {
            if (_motors[i]) {
                delete _motors[i];
            }
        }
    }
}
