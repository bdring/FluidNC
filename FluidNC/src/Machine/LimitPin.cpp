#include "LimitPin.h"
#include "Axes.h"
#include "MachineConfig.h"  // config

#include "../NutsBolts.h"      // set_bitnum etc
#include "../MotionControl.h"  // mc_reset
#include "../Limits.h"
#include "../Protocol.h"  // rtAlarm

#include <esp32-hal-gpio.h>  // CHANGE

namespace Machine {
    LimitPin::LimitPin(Pin& pin, int axis, int motor, int direction, bool& pHardLimits) :
        _axis(axis), _motorNum(motor), _value(false), _pHardLimits(pHardLimits), _pin(pin) {
        String sDir;
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
        _legend  = String("    " + sDir + " Limit");
    }

    void IRAM_ATTR LimitPin::handleISR() {
        read();
        if (sys.state != State::Alarm && sys.state != State::ConfigAlarm && sys.state != State::Homing) {
            if (_pHardLimits && rtAlarm == ExecAlarm::None) {
#if 0

                if (config->_softwareDebounceMs) {
                    // send a message to wakeup the task that rechecks the switches after a small delay
                    int evt;
                    xQueueSendFromISR(limit_sw_queue, &evt, NULL);
                    return;
                }
#endif

                // log_debug("Hard limits");  // This might not work from ISR context
                mc_reset();                      // Initiate system kill.
                rtAlarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
            }
        }
    }

    void IRAM_ATTR LimitPin::read() {
        _value = _pin.read();
        if (_value) {
            if (_posLimits != nullptr) {
                set_bits(*_posLimits, _bitmask);
            }
            if (_negLimits != nullptr) {
                set_bits(*_negLimits, _bitmask);
            }
        } else {
            if (_posLimits != nullptr) {
                clear_bits(*_posLimits, _bitmask);
            }
            if (_negLimits != nullptr) {
                clear_bits(*_negLimits, _bitmask);
            }
        }
    }

    void LimitPin::init() {
        if (_pin.undefined()) {
            return;
        }
        set_bitnum(Axes::limitMask, _axis);
        _pin.report(_legend.c_str());
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (_pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        _pin.setAttr(attr);
        _pin.attachInterrupt(ISRHandler, CHANGE, this);

        read();
    }

    // Make this switch act like an axis level switch. Both motors will report the same
    // This should be called from a higher level object, that has the logic to figure out
    // if this belongs to a dual motor, single switch axis
    void LimitPin::makeDualMask() { _bitmask = Axes::axes_to_motors(Axes::motors_to_axes(_bitmask)); }

    LimitPin::~LimitPin() { _pin.detachInterrupt(); }
}
