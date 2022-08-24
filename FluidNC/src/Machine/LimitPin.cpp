#include "LimitPin.h"
#include "Axes.h"
#include "MachineConfig.h"  // config

#include "src/NutsBolts.h"      // set_bitnum etc
#include "src/MotionControl.h"  // mc_reset
#include "src/Limits.h"
#include "src/Protocol.h"  // protocol_send_event_from_ISR()

#include "soc/soc.h"
#include "soc/gpio_periph.h"
#include "hal/gpio_hal.h"

namespace Machine {
    TimerHandle_t LimitPin::_limitTimer = 0;
    void          LimitPin::limitTimerCallback(void*) { checkLimits(); }

    std::list<LimitPin*> LimitPin::_blockedLimits;

    LimitPin::LimitPin(Pin& pin, int axis, int motor, int direction, bool& pHardLimits, bool& pLimited) :
        _axis(axis), _motorNum(motor), _value(false), _pHardLimits(pHardLimits), _pLimited(pLimited), _pin(pin) {
        if (_limitTimer == 0) {
            _limitTimer = xTimerCreate("limitTimer", pdMS_TO_TICKS(500), false, NULL, limitTimerCallback);
        }

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
        _gpio    = _pin.getNative(Pin::Capabilities::Input | Pin::Capabilities::ISR);
    }

    bool IRAM_ATTR LimitPin::read() {
        _value    = _pin.read();
        _pLimited = _value;
        if (_pExtraLimited != nullptr) {
            *_pExtraLimited = _value;
        }
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
        return _value;
    }

    void IRAM_ATTR LimitPin::handleISR() {
        // This is the body of gpio_hal_intr_disable() which is not IRAM_ATTR
        gpio_num_t  gpio_num = gpio_num_t(_gpio);
        gpio_dev_t* dev      = GPIO_LL_GET_HW(GPIO_PORT_0);
        gpio_ll_intr_disable(dev, gpio_num);
        if (gpio_num < 32) {
            gpio_ll_clear_intr_status(dev, BIT(gpio_num));
        } else {
            gpio_ll_clear_intr_status_high(dev, BIT(gpio_num - 32));
        }
        protocol_send_event_from_ISR(this);
    }

    bool LimitPin::limitInactive(LimitPin* pin) {
        auto value = pin->read();
        if (value) {
            log_debug("limit for " << config->_axes->axisName(pin->_axis) << " motor " << pin->_motorNum << " is still active");
            if (sys.state != State::Homing) {
                xTimerStart(_limitTimer, 0);
            }
        } else {
            log_debug("Reenabling limit for " << config->_axes->axisName(pin->_axis) << " motor " << pin->_motorNum);
            pin->enableISR();
        }
        return !value;
    }

    void LimitPin::checkLimits() {
#if 1
        _blockedLimits.remove_if(limitInactive);

#else
        while (!_blockedLimits.empty()) {
            auto pin = _blockedLimits.front();
            _blockedLimits.pop();
            auto value = pin->read();  // To reestablish the limit bitmasks
            if (value) {}
            log_debug("Reenabling limit for " << config->_axes->axisName(pin->_axis) << " motor " << pin->_motorNum << " value " << value);
            pin->enableISR();
        }
#endif
    }

    void LimitPin::enableISR() { gpio_intr_enable(gpio_num_t(_gpio)); }

    void LimitPin::run(void* arg) {
        bool active = read();
        if (!active) {
            enableISR();
            return;
        }
        _blockedLimits.emplace_back(this);
        if (sys.state == State::Homing) {
            Machine::Homing::limitReached();
            return;
        }
        if (sys.state == State::Cycle) {
            if (_pHardLimits && rtAlarm == ExecAlarm::None) {
                log_debug("Hard limits");
                mc_reset();                      // Initiate system kill.
                rtAlarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
            }
            return;
        }
        log_debug("Limit switch tripped for " << config->_axes->axisName(_axis) << " motor " << _motorNum);
        xTimerStart(_limitTimer, 0);
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
        _pin.attachInterrupt(ISRHandler, Pin::EITHER_EDGE, this);

        read();
    }

    // Make this switch act like an axis level switch. Both motors will report the same
    // This should be called from a higher level object, that has the logic to figure out
    // if this belongs to a dual motor, single switch axis
    void LimitPin::makeDualMask() { _bitmask = Axes::axes_to_motors(Axes::motors_to_axes(_bitmask)); }

    void LimitPin::setExtraMotorLimit(int axis, int motorNum) { _pExtraLimited = &config->_axes->_axis[axis]->_motors[motorNum]->_limited; }

    LimitPin::~LimitPin() { _pin.detachInterrupt(); }
}
