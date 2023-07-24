#include "src/Machine/EventPin.h"
#include "src/Machine/Axes.h"
#include "src/Machine/MachineConfig.h"  // config

#include "src/Limits.h"
#include "src/Protocol.h"  // protocol_send_event_from_ISR()

namespace Machine {
    LimitPin::LimitPin(Pin& pin, int axis, int motor, int direction, bool& pHardLimits, bool& pLimited) :
        EventPin(&limitEvent, "Limit", &pin), _axis(axis), _motorNum(motor), _value(false), _pHardLimits(pHardLimits), _pLimited(pLimited) {
        const char* sDir;
        // Select one or two bitmask variables to receive the switch data
        switch (direction) {
            case 1:
                _posLimits = &Axes::posLimitMask;
                _negLimits = nullptr;
                sDir       = "Pos";
                break;
            case -1:
                _posLimits = nullptr;
                _negLimits = &Axes::negLimitMask;
                sDir       = "Neg";
                break;
            case 0:
                _posLimits = &Axes::posLimitMask;
                _negLimits = &Axes::negLimitMask;
                sDir       = "All";
                break;
            default:  // invalid
                _negLimits   = nullptr;
                _posLimits   = nullptr;
                _pHardLimits = false;
                break;
        }

        // Set a bitmap with bits to represent the axis and which motors are affected
        // The bitmap looks like CBAZYX..cbazyx where motor0 motors are in the lower bits
        _bitmask = 1 << Axes::motor_bit(axis, motor);
        _legend  = config->_axes->motorMaskToNames(_bitmask);
        _legend += " ";
        _legend += sDir;
        _legend += " Limit";
    }

    void LimitPin::init() {
        EventPin::init();
        if (_pin->undefined()) {
            return;
        }
        update(get());
    }

    void LimitPin::update(bool value) {
        log_debug(_legend << " " << value);
        if (value) {
            if (Homing::approach() || (sys.state != State::Homing && _pHardLimits)) {
                _pLimited = value;

                if (_pExtraLimited != nullptr) {
                    *_pExtraLimited = value;
                }
            }

            if (_posLimits != nullptr) {
                set_bits(*_posLimits, _bitmask);
            }
            if (_negLimits != nullptr) {
                set_bits(*_negLimits, _bitmask);
            }
        } else {
            _pLimited = value;

            if (_pExtraLimited != nullptr) {
                *_pExtraLimited = value;
            }
            if (_posLimits != nullptr) {
                clear_bits(*_posLimits, _bitmask);
            }
            if (_negLimits != nullptr) {
                clear_bits(*_negLimits, _bitmask);
            }
        }
    }

    // Make this switch act like an axis level switch. Both motors will report the same
    // This should be called from a higher level object, that has the logic to figure out
    // if this belongs to a dual motor, single switch axis
    void LimitPin::makeDualMask() { _bitmask = Axes::axes_to_motors(Axes::motors_to_axes(_bitmask)); }

    void LimitPin::setExtraMotorLimit(int axis, int motorNum) { _pExtraLimited = &config->_axes->_axis[axis]->_motors[motorNum]->_limited; }
}
