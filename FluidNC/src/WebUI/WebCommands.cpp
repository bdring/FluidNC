// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  WebCommands.cpp - Settings and Commands for the interface
  to ESP3D_WebUI.  Code snippets extracted from commands.cpp in the
  old WebUI interface code are presented via the Settings class.
*/

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include "Configuration/JsonGenerator.h"
#include "Report.h"  // git_info

#include <Esp.h>

#include <sstream>
#include <iomanip>

#include "Module.h"

namespace WebUI {

    class WebCommands : public Module {
    public:
        WebCommands(const char* name) : Module(name) {}

        static std::string LeftJustify(const char* s, size_t width) {
            std::string ret(s);

            for (size_t l = ret.length(); width > l; width--) {
                ret += ' ';
            }
            return ret;
        }

        // Used by js/connectdlg.js

#ifdef ENABLE_AUTHENTICATION
        static Error setUserPassword(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP555
            if (*parameter == '\0') {
                user_password->setDefault();
                return Error::Ok;
            }
            if (user_password->setStringValue(parameter) != Error::Ok) {
                log_string(out, "Invalid Password");
                return Error::InvalidValue;
            }
            return Error::Ok;
        }
#endif

        static Error restart(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            log_info("Restarting");
            protocol_send_event(&fullResetEvent);
            return Error::Ok;
        }

        // used by js/restartdlg.js
        static Error setSystemMode(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP444
            // parameter = trim(parameter);
            if (strcasecmp(parameter, "RESTART") != 0) {
                log_string(out, "Parameter must be RESTART");
                return Error::InvalidValue;
            }
            return restart(parameter, auth_level, out);
        }

        // Used by js/statusdlg.js
        static Error showSysStatsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420

            JSONencoder j(&out);
            j.begin();
            j.member("cmd", "420");
            j.member("status", "ok");
            j.begin_array("data");

            j.id_value_object("Chip ID", (uint16_t)(ESP.getEfuseMac() >> 32));
            j.id_value_object("CPU Cores", ESP.getChipCores());

            std::ostringstream msg;
            msg << ESP.getCpuFreqMHz() << "Mhz";
            j.id_value_object("CPU Frequency", msg.str());

            std::ostringstream msg2;
            msg2 << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
            j.id_value_object("CPU Temperature", msg2.str());

            j.id_value_object("Free memory", formatBytes(ESP.getFreeHeap()));
            j.id_value_object("SDK", ESP.getSdkVersion());
            j.id_value_object("Flash Size", formatBytes(ESP.getFlashChipSize()));

            for (auto const& module : ModuleFactory::objects()) {
                module->wifi_stats(j);
            }

            std::string s("FluidNC ");
            s += git_info;
            j.id_value_object("FW version", s);

            j.end_array();
            j.end();
            return Error::Ok;
        }

        static void send_json_command_response(Channel& out, uint cmdID, bool isok, const std::string& message) {
            JSONencoder j(&out);
            j.begin();
            j.member("cmd", String(cmdID).c_str());
            j.member("status", isok ? "ok" : "error");
            j.member("data", message);
            j.end();
        }

        static Error showSysStats(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420
            if (paramIsJSON(parameter)) {
                return showSysStatsJSON(parameter, auth_level, out);
            }

            log_stream(out, "Chip ID: " << (uint16_t)(ESP.getEfuseMac() >> 32));
            log_stream(out, "CPU Cores: " << ESP.getChipCores());
            log_stream(out, "CPU Frequency: " << ESP.getCpuFreqMHz() << "Mhz");

            std::ostringstream msg;
            msg << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
            log_stream(out, "CPU Temperature: " << msg.str());
            log_stream(out, "Free memory: " << formatBytes(ESP.getFreeHeap()));
            log_stream(out, "SDK: " << ESP.getSdkVersion());
            log_stream(out, "Flash Size: " << formatBytes(ESP.getFlashChipSize()));

            for (auto const& module : Modules()) {
                module->build_info(out);
            }

            log_stream(out, "FW version: FluidNC " << git_info);
            return Error::Ok;
        }

        static Error setWebSetting(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP401
            // The string is of the form "P=name T=type V=value
            // We do not need the "T=" (type) parameter because the
            // Setting objects know their own type.  We do not use
            // split_params because if fails if the value string
            // contains '='
            std::string p, v;
            bool        isJSON = paramIsJSON(parameter);
            if (!(get_param(parameter, "P=", p) && get_param(parameter, "V=", v))) {
                if (isJSON) {
                    send_json_command_response(out, 401, false, errorString(Error::InvalidValue));
                }
                return Error::InvalidValue;
            }

            Error ret = do_command_or_setting(p, v, auth_level, out);
            if (isJSON) {
                send_json_command_response(out, 401, ret == Error::Ok, errorString(ret));
            }

            return ret;
        }

        // Used by js/setting.js
        static Error listSettingsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
            JSONencoder j(&out);
            j.begin();
            j.member("cmd", "400");
            j.member("status", "ok");
            j.begin_array("data");

            // NVS settings
            j.setCategory("Flash/Settings");
            for (Setting* js : Setting::List) {
                js->addWebui(&j);
            }

            // Configuration tree
            j.setCategory("Running/Config");
            Configuration::JsonGenerator gen(j);
            config->group(gen);

            j.end_array();
            j.end();

            return Error::Ok;
        }

        static Error listSettings(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
            if (parameter != NULL) {
                if (strstr(parameter, "json=yes") != NULL) {
                    return listSettingsJSON(parameter, auth_level, out);
                }
            }

            JSONencoder j(&out);

            j.begin();
            j.begin_array("EEPROM");

            // NVS settings
            j.setCategory("nvs");
            for (Setting* js : Setting::List) {
                js->addWebui(&j);
            }

            // Configuration tree
            j.setCategory("tree");
            Configuration::JsonGenerator gen(j);
            config->group(gen);

            j.end_array();
            j.end();

            return Error::Ok;
        }

        static Error showWebHelp(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP0
            log_string(out, "Persistent web settings - $name to show, $name=value to set");
            log_string(out, "ESPname FullName         Description");
            log_string(out, "------- --------         -----------");

            for (Setting* setting : Setting::List) {
                if (setting->getType() == WEBSET) {
                    log_stream(out,
                               LeftJustify(setting->getGrblName() ? setting->getGrblName() : "", 8)
                                   << LeftJustify(setting->getName(), 25 - 8) << setting->getDescription());
                }
            }
            log_string(out, "");
            log_string(out, "Other web commands: $name to show, $name=value to set");
            log_string(out, "ESPname FullName         Values");
            log_string(out, "------- --------         ------");

            for (Command* cp : Command::List) {
                if (cp->getType() == WEBCMD) {
                    LogStream s(out, "");
                    s << LeftJustify(cp->getGrblName() ? cp->getGrblName() : "", 8) << LeftJustify(cp->getName(), 25 - 8);
                    if (cp->getDescription()) {
                        s << cp->getDescription();
                    }
                }
            }
            return Error::Ok;
        }

        void init() override {
            // If authentication enabled, display_settings skips or displays <Authentication Required>
            // RU - need user or admin password to read
            // WU - need user or admin password to set
            // WA - need admin password to set
            new WebCommand(NULL, WEBCMD, WU, "ESP420", "System/Stats", showSysStats, anyState);
            new WebCommand("RESTART", WEBCMD, WA, "ESP444", "System/Control", setSystemMode);

            //      new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

            new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
            new WebCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
            new WebCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
            new WebCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);
        }
    };
    ModuleFactory::InstanceBuilder<WebCommands> web_commands_module __attribute__((init_priority(103))) ("web_commands", true);
}
