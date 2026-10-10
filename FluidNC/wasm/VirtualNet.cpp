// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Virtual network module for the wasm build -- the wasm counterpart of
// WifiConfig/EthConfig (and of NetConfig for the posix ports).
//
// A browser page cannot own a network interface, so this "interface" is the
// JS layer that carries HTTP and WebSocket connections into
// BrowserAsyncTCP (a Service Worker for HTTP, a WebSocket stand-in for WS).
// It is always up, has a fixed address (see virtual_ip), and its hostname is the
// shared $Hostname setting.  The address is cosmetic: the demo intercepts
// every request regardless of the host it names, but WebUI2 builds its
// ws:// URL from [ESP800]'s WebSocketIP/WebSocketPort, so those must at
// least be consistent with the HTTP server.
//
// It stands in for WebUI/NetConfig.cpp, the posix ports' network module,
// whose IP comes from the host OS via Arduino-Emulator.
//
// networkEnabled()/networkConnected() (WebUI/NetSettings.cpp) still answer
// through Arduino-Emulator's WifiMock, which always reports STA/connected.

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include "Channel.h"
#include "Error.h"
#include "Module.h"
#include "Report.h"  // git_info
#include "Driver/localfs.h"
#include "WebUI/Authentication.h"  // AuthenticationLevel
#include "WebUI/NetSettings.h"     // _hostname
#include "WebUI/WebUIServer.h"     // WebUI_Server::port()
#include "WebUI/NotificationsService.h"

#include <string>
#include <cstring>

namespace WebUI {
    // FluidNC's default address in access-point mode, so the demo looks like
    // a controller you have joined directly.
    static const char* virtual_ip = "192.168.0.1";

    std::string webServerIp() {
        return virtual_ip;
    }
    std::string myHostname() {
        return _hostname ? _hostname->get() : "fluidnc";
    }

    class VirtualNet : public Module {
    private:
        static Error showIP(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
            log_stream(out, parameter << webServerIp());
            return Error::Ok;
        }

        static Error showFwInfoJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            JSONencoder j(&out);
            j.begin();
            j.member("cmd", "800");
            j.member("status", "ok");
            j.begin_member_object("data");
            j.member("FWVersion", git_info);
            j.member("FWTarget", "FluidNC");
            j.member("FWTargetId", "60");
            j.member("WebUpdate", "Disabled");
            j.member("Setup", "Disabled");
            j.member("SDConnection", "direct");
            j.member("SerialProtocol", "Socket");
            j.member("Authentication", "Disabled");
            j.member("WebCommunication", "Synchronous");
            j.member("WebSocketIP", webServerIp());
            j.member("WebSocketPort", std::to_string(WebUI_Server::port()));
            j.member("HostName", myHostname());
            j.member("WiFiMode", "Virtual");
            j.member("FlashFileSystem", "LittleFS");
            j.member("HostPath", "/");
            j.member("Time", "none");
            std::string axisLetters;
            for (axis_t axis = X_AXIS; axis < Axes::_numberAxis; axis++) {
                axisLetters += Axes::axisName(axis);
            }
            j.member("Axisletters", axisLetters);
            j.end_object();
            j.end();
            return Error::Ok;
        }

        static Error showFwInfo(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            if (parameter != NULL && paramIsJSON(parameter)) {
                return showFwInfoJSON(parameter, auth_level, out);
            }
            LogStream s(out, "FW version: FluidNC ");
            s << git_info;
            s << " # FW target:grbl-embedded  # FW HW:";
            s << ((config->_sdCard && config->_sdCard->config_ok) ? "Direct SD" : "None");
            s << " # primary sd:" << ((config->_sdCard && config->_sdCard->config_ok) ? "/sd" : "none");
            s << " # secondary sd:none ";
            s << " # authentication:no";
            s << " # webcommunication: Sync: " << std::to_string(WebUI_Server::port());
            s << " # hostname:" << myHostname();
            s << " # axis:" << Axes::_numberAxis;
            return Error::Ok;
        }

    public:
        VirtualNet(const char* name) : Module(name) {}

        void init() override {
            new WebReportCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState);
            new WebReportCommand(NULL, WEBCMD, WG, "ESP111", "System/IP", showIP, anyState);
            log_info("Virtual network: IP=" << webServerIp() << " Hostname=" << myHostname());
        }

        // Shown in response to $I, analogous to WiFiConfig/EthConfig::build_info()
        void build_info(Channel& channel) override {
            log_msg_to(channel, "Mode=Virtual:IP=" << webServerIp() << ":Hostname=" << myHostname());
        }

        // Shown in response to [ESP420]
        void wifi_stats(JSONencoder& j) override {
            j.id_value_object("Current Network Mode", "Virtual (browser)");
            j.id_value_object("Available Size for LocalFS", formatBytes(localfs_size()));
            j.id_value_object("Web port", WebUI_Server::port());
            j.id_value_object("Hostname", myHostname());
            j.id_value_object("IP", webServerIp());
            j.id_value_object("Notifications",
                              NotificationsService::started() ? std::string("Enabled(") + NotificationsService::getTypeString() + ")"
                                                              : std::string("Disabled"));
        }

        bool is_radio() override { return false; }

        ~VirtualNet() {}
    };

    // 105: after NetSettings (104), which creates _hostname; before
    // WebUI_Server (108).
    ModuleFactory::InstanceBuilder<VirtualNet> __attribute__((init_priority(105))) virtual_net_module("virtualnet", true);
}
