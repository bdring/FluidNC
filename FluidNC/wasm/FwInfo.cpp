// Provides the ESP800 (Firmware/Info) command that a real WebUI build
// sends as its connect handshake (see demo/index.html's shim bridge). The
// desktop native ports (macos/linux/windows_x86) get this for free from
// WebUI/NetConfig.cpp, but that file is WiFi-specific (webServerIp(),
// hostname, WebUI_Server::port()) and pulled in only by -<WebUI/NetConfig.cpp>
// exclusions elsewhere -- this port excludes all of WebUI/ instead (see
// platformio.ini), so it needs its own minimal handler with stub
// network fields ESP3D-WEBUI reads but doesn't act on when talking to
// this postMessage bridge instead of a real WebSocket.

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include "Configuration/JsonGenerator.h"
#include "Channel.h"
#include "Error.h"
#include "Module.h"
#include "Report.h"  // git_info

namespace WebUI {
    class FwInfo : public Module {
    private:
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
            j.member("WebSocketIP", "127.0.0.1");
            j.member("WebSocketPort", "81");
            j.member("HostName", "wasm");
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
            s << " # secondary sd:none ";
            s << " # authentication:no";
            s << " # webcommunication: Sync: 81";
            s << " # hostname:wasm";
            s << " # axis:" << Axes::_numberAxis;
            return Error::Ok;
        }

    public:
        FwInfo(const char* name) : Module(name) {}
        void init() { new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState); }
        ~FwInfo() {}
    };

    ModuleFactory::InstanceBuilder<FwInfo> __attribute__((init_priority(105))) fw_info_module("fw_info", true);
}
