// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include <sstream>
#include <iomanip>
#include <unistd.h>

#include "Channel.h"         // Channel
#include "Error.h"           // Error
#include "Module.h"          // Module
#include "Authentication.h"  // AuthenticationLevel

#include "Main.h"

#include <AsyncTCP.h>
#include "WebUIServer.h"           // Web_Server::port()
#include "TelnetServer.h"          // TelnetServer::port()
#include "NotificationsService.h"  // notificationsservice

#include <WiFi.h>
#include "Driver/localfs.h"
#include <string>
#include <cstring>

#define Network WiFi

namespace WebUI {
    std::string webServerIp() {
        return IP_string(Network.localIP());
    }
    std::string myHostname() {
        char name[64];
        gethostname(name, 63);
        return std::string(name);
    }

#ifdef HAVE_DNS
    const byte DNS_PORT = 53;
    DNSServer  dnsServer;
#endif
    class NetConfig : public Module {
    private:
        static constexpr int MAX_HOSTNAME_LENGTH = 32;
        static constexpr int MIN_HOSTNAME_LENGTH = 1;
        static Error         showIP(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
            log_stream(out, parameter << webServerIp());
            return Error::Ok;
        }

        void status_report(Channel& out) {
            log_stream(out, "Available Size for LocalFS: " << formatBytes(localfs_size()));
            log_stream(out, "Web port: " << WebUI_Server::port());
            log_stream(out, "hostname: " << "localhost");

            log_stream(out, "IP: " << IP_string(Network.localIP()));

            LogStream s(out, "Notifications: ");
            s << (NotificationsService::started() ? "Enabled" : "Disabled");
            if (NotificationsService::started()) {
                s << "(" << NotificationsService::getTypeString() << ")";
            }
        }

        static Error showFwInfoJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            if (strstr(parameter, "json=yes") != NULL) {
                JSONencoder j(&out);
                j.begin();
                j.member("cmd", "800");
                j.member("status", "ok");
                j.begin_member_object("data");
                j.member("FWVersion", git_info);
                j.member("FWTarget", "FluidNC");
                j.member("FWTargetId", "60");
                j.member("WebUpdate", "Enabled");

                j.member("Setup", "Disabled");
                j.member("SDConnection", "direct");
                j.member("SerialProtocol", "Socket");
#ifdef ENABLE_AUTHENTICATION
                j.member("Authentication", "Enabled");
#else
                j.member("Authentication", "Disabled");
#endif
                j.member("WebCommunication", "Synchronous");

                j.member("WebSocketIP", webServerIp());

                j.member("WebSocketPort", std::to_string(WebUI_Server::port()));
                j.member("HostName", "localhost");
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

            return Error::InvalidStatement;
        }
        static Error showFwInfo(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            if (parameter != NULL && paramIsJSON(parameter)) {
                return showFwInfoJSON(parameter, auth_level, out);
            }
            LogStream s(out, "FW version: FluidNC ");
            s << git_info;
            // TODO: change grbl-embedded to FluidNC after fixing WebUI
            s << " # FW target:grbl-embedded  # FW HW:";

            // We do not check the SD presence here because if the SD card is out,
            // WebUI will switch to M20 for SD access, which is wrong for FluidNC
            s << "Direct SD";

            s << "  # primary sd:";

            (config->_sdCard->config_ok) ? s << "/sd" : s << "none";

            s << " # secondary sd:none ";

            s << " # authentication:";
#ifdef ENABLE_AUTHENTICATION
            s << "yes";
#else
            s << "no";
#endif
            s << " # webcommunication: Sync: ";
            s << std::to_string(WebUI_Server::port());
#if 0
            // If we omit the explicit IP address for the websocket,
            // WebUI will use the same IP address that it uses for
            // HTTP, with the port number as above.  That is better
            // than providing an explicit address, because if the WiFi
            // drops and comes back up again, DHCP might assign a
            // different IP address so the one provided below would no
            // longer work.  But if we are using an MDNS address like
            // fluidnc.local, a websocket reconnection will succeed
            // because MDNS will offer the new IP address.
            s << ":";
            s << IP_string(Network.localIP());
#endif
            s << " # hostname:";
            s << "localhost";

            //to save time in decoding `?`
            s << " # axis:" << Axes::_numberAxis;
            return Error::Ok;
        }

        static bool isOn() { return true; }

    public:
        NetConfig(const char* name) : Module(name) {}
        void init() { new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState); }
        ~NetConfig() {}
    };

    ModuleFactory::InstanceBuilder<NetConfig> __attribute__((init_priority(105))) net_module("network", true);
}
