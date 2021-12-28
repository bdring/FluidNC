#include "SimpleProbe.h"

namespace Probes {
    const char* SimpleProbe::name() const { return "simple_probe"; }

    void SimpleProbe::validate() const {}
    void SimpleProbe::group(Configuration::HandlerBase& handler) {
        handler.item("pin", _probePin);
        handler.item("check_mode_start", _checkModeStart);
    }

    void SimpleProbe::init(TripProbe* callback) {
        ProbeDriver::init(callback);
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
