#include "ToolSetter.h"

namespace Probes {
    const char* ToolSetter::name() const { return "tool_setter"; }

    // Configuration handlers:
    void ToolSetter::validate() const { SimpleProbe::validate(); }
    void ToolSetter::group(Configuration::HandlerBase& handler) {
        SimpleProbe::group(handler);
        handler.item("tool_setter_height", _height);
    }

    void ToolSetter::init(TripProbe* callback) { SimpleProbe::init(callback); }
    void ToolSetter::start_cycle() {
        // TODO FIXME: reset soft limits to original
        SimpleProbe::start_cycle();
    }
    void ToolSetter::stop_cycle() {
        SimpleProbe::stop_cycle();
        // TODO FIXME: set soft limits with height
    }

    bool ToolSetter::is_tripped() { return _probePin.read(); }

    ToolSetter::~ToolSetter() {}

    // Configuration registration
    namespace {
        ProbeFactory::InstanceBuilder<ToolSetter> registration("tool_setter");
    }
}
