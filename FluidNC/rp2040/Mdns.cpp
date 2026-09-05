#include "Module.h"
#include "LEAmDNS.h"
#include "Driver/fluidnc_mdns.h"
#include <WiFi.h>

namespace WebUI {
    EnumSetting*  Mdns::_enable;
    MDNSResponder mdns;

    void Mdns::init() {
        _enable = new EnumSetting("mDNS enable", WEBSET, WA, NULL, "MDNS/Enable", true, &onoffOptions);

        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            const char* h = WiFi.getHostname();
            MDNS.begin(h);
            log_info("Start mDNS with hostname:http://" << h << ".local/");
        }
    }

    void Mdns::deinit() {
        MDNS.end();
    }
    void Mdns::add(const char* service, const char* proto, uint16_t port) {
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            MDNS.addService(0, service, proto, port);
            MDNS.announce();
        }
    }
    void Mdns::remove(const char* service, const char* proto) {
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            MDNS.removeService(0, service, proto);
            MDNS.announce();
        }
    }
    void Mdns::poll() {
        MDNS.update();
    }

    ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
}
