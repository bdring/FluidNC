// The native simulator has no mDNS responder, but TelnetServer and
// WebUI_Server still advertise themselves through Mdns::add/remove.

#include "Module.h"
#include "Driver/fluidnc_mdns.h"

namespace WebUI {
    EnumSetting* Mdns::_enable;

    void Mdns::init() {}
    void Mdns::deinit() {}
    void Mdns::poll() {}
    void Mdns::add(const char* service, const char* proto, uint16_t port) {}
    void Mdns::remove(const char* service, const char* proto) {}
}
