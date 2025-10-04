#include "PlasmaSpindle.h"

#include "System.h"  // sys.abort

/*

PlasmaSpindle:
  output_pin: gpio.13
  enable_pin: gpio.14
  arc_ok_pin: 'gpio.33:low'
  arc_wait_ms: 1200
  tool_num: 0
  speed_map: 0=0.00% 1=100.00%
  off_on_alarm: true
  atc:
  m6_macro:

Ideas:

 - Maybe arc_wait_ms disables that feature

*/

namespace Spindles {

    //    PlasmaSpindle::ArcOkEventPin PlasmaSpindle::_arcOkEventPin(this);

    void PlasmaSpindle::init() {
        _arcOkEventPin.init();

        _arc_on = false;

        _enable_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            // The default speed map for an On/Off spindle is off - 0% -
            // for speed 0 and on - 100% - for any nonzero speedl
            // In other words there is a step transition right at 0.
            linearSpeeds(1, 100.0f);
        }
        setupSpeeds(1);
        init_atc();
        config_message();
    }

    // prints the startup message of the spindle config
    void PlasmaSpindle ::config_message() {
        //        log_info(name() << " Ena:" << _enable_pin.name() << " Arc OK:" << _arcOkEventPin.pin().name() << atc_info());
        log_info(name() << " Ena:" << _enable_pin.name() << " Arc OK:" << _arcOkEventPin.name() << atc_info());
    }

    void PlasmaSpindle::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort()) {
            return;  // Block during abort.
        }

        if (state == SpindleState::Disable) {
            _arc_on = false;
            set_enable(false);
            sys.set_spindle_speed(0);
        } else {
            sys.set_spindle_speed(speed);

            // if the spindle was already enabled this was just a speed change
            if (gc_state.modal.spindle != SpindleState::Disable) {
                return;
            }

            // check arc OK is not on before starting
            if (_arcOkEventPin.get()) {
                log_error(name() << " arc_ok active before starting plasma");
                mc_critical(ExecAlarm::SpindleControl);
                return;
            }

            set_enable(true);

            if (!wait_for_arc_ok()) {
                return;
            }
            _arc_on = true;
        }
    }

    bool IRAM_ATTR PlasmaSpindle::wait_for_arc_ok() {
        uint32_t wait_until_ms = get_ms() + _max_arc_wait;
        while (get_ms() < wait_until_ms) {
            if (_arcOkEventPin.get()) {
                _arc_on = true;
                return true;
            }
            protocol_execute_realtime();
            delay_ms(1);
        }
        _arc_on                = false;
        gc_state.modal.spindle = SpindleState::Disable;
        mc_critical(ExecAlarm::SpindleControl);
        log_error(name() << " failed to get arc OK signal");
        return false;  // failed to get arc_ok
    }

    void IRAM_ATTR PlasmaSpindle::set_output(uint32_t dev_speed) {}

    void IRAM_ATTR PlasmaSpindle::setSpeedfromISR(uint32_t dev_speed) {}

    void IRAM_ATTR PlasmaSpindle::set_enable(bool enable) {
        if (_disable_with_zero_speed && sys.spindle_speed() == 0) {
            enable = false;
        }

        _enable_pin.synchronousWrite(enable);
    }

    void PlasmaSpindle::set_direction(bool Clockwise) {}

    void PlasmaSpindle::deinit() {
        stop();
        _enable_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<PlasmaSpindle> registration("PlasmaSpindle");
    }
}
