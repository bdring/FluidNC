#include "SimpleProbe.h"

namespace Probes {
    const char* SimpleProbe::name() const { return "simple_probe"; }

    void SimpleProbe::validate() const {}
    void SimpleProbe::group(Configuration::HandlerBase& handler) {
        handler.item("pin", _probePin);
        handler.item("check_mode_start", _checkModeStart);
    }

    void SimpleProbe::init(TripProbe callback) {
        if (_probePin.defined()) {
            _probePin.setAttr(Pin::Attr::Input);
        }

        // if (show_init_msg) {
        //     _probePin.report("Probe Pin:");
        //     show_init_msg = false;
        // }

        ProbeDriver::init(callback, callbackUserData_);
        _probePin.attachInterrupt<ProbeDriver, &ProbeDriver::tripISR>(this);
    }

    void SimpleProbe::start_cycle() {}
    void SimpleProbe::stop_cycle() {}
    bool SimpleProbe::is_tripped() { return _probePin.read(); }

    // Configuration registration
    namespace {
        ProbeFactory::InstanceBuilder<SimpleProbe> registration("simple_probe");
    }
}
