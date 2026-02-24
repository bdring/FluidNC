// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include <sstream>
#include <iomanip>

#include "Channel.h"         // Channel
#include "Error.h"           // Error
#include "Module.h"          // Module
#include "Authentication.h"  // AuthenticationLevel

#include "Main.h"

#include "WebUIServer.h"           // Web_Server::port()
#include "TelnetServer.h"          // TelnetServer::port()
#include "NotificationsService.h"  // notificationsservice

#include <WiFi.h>
#include "Driver/localfs.h"
#include <string>
#include <cstring>
#include "WifiImpl.h"

namespace WebUI {
    enum WiFiStartupMode {
        WiFiOff = 0,
        WiFiSTA,
        WiFiAP,
        WiFiFallback,  // Try STA and fall back to AP if STA fails
    };

    const enum_opt_t wifiModeOptions = {
        { "Off", WiFiOff },
        { "STA", WiFiSTA },
        { "AP", WiFiAP },
        { "STA>AP", WiFiFallback },
    };

    static const char* NULL_IP = "0.0.0.0";

    //boundaries
    static constexpr int MAX_SSID_LENGTH     = 32;
    static constexpr int MIN_SSID_LENGTH     = 0;  // Allow null SSIDs as a way to disable
    static constexpr int MAX_PASSWORD_LENGTH = 64;
    //min size of password is 0 or upper than 8 char
    //so let set min is 8
    static constexpr int MIN_PASSWORD_LENGTH = 8;
    static constexpr int MAX_HOSTNAME_LENGTH = 32;
    static constexpr int MIN_HOSTNAME_LENGTH = 1;

    static constexpr int DHCP_MODE   = 0;
    static constexpr int STATIC_MODE = 1;

    static const enum_opt_t staModeOptions = {
        { "DHCP", DHCP_MODE },
        { "Static", STATIC_MODE },
    };

    class PasswordSetting : public StringSetting {
    public:
        PasswordSetting(const char* description, const char* grblName, const char* name, const char* defVal) :
            StringSetting(description, WEBSET, WA, grblName, name, defVal, MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH) {
            load();
        }
        const char* getDefaultString() { return "********"; }
        const char* getStringValue() { return "********"; }
    };

    class HostnameSetting : public StringSetting {
    public:
        HostnameSetting(const char* description, const char* grblName, const char* name, const char* defVal) :
            StringSetting(description, WEBSET, WA, grblName, name, defVal, MIN_HOSTNAME_LENGTH, MAX_HOSTNAME_LENGTH) {
            load();
        }
        Error setStringValue(std::string_view s) {
            // Hostname strings may contain only letters, digits and -
            for (auto const& c : s) {
                if (c == ' ' || !(isdigit(c) || isalpha(c) || c == '-')) {
                    return Error::InvalidValue;
                }
            }
            return StringSetting::setStringValue(s);
        }
    };

    static EnumSetting*     _mode;
    static StringSetting*   _sta_ssid;
    static HostnameSetting* _hostname;
    static IntSetting*      _ap_channel;
    static IPaddrSetting*   _ap_ip;
    static PasswordSetting* _ap_password;
    static StringSetting*   _ap_ssid;
    static EnumSetting*     _ap_country;
    static IPaddrSetting*   _sta_netmask;
    static IPaddrSetting*   _sta_gateway;
    static IPaddrSetting*   _sta_ip;
    static EnumSetting*     _sta_mode;
    static EnumSetting*     _fast_scan;
    static EnumSetting*     _sta_min_security;
    static PasswordSetting* _sta_password;
    static EnumSetting*     _wifi_ps_mode;

    class WiFiConfig : public Module {
    private:
        static void print_mac(Channel& out, const char* prefix, const char* mac) { log_stream(out, prefix << " (" << mac << ")"); }

        static Error showIP(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
            log_stream(out, parameter << IP_string(WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP()));
            return Error::Ok;
        }

        static Error showSetStaParams(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP103
            if (*parameter == '\0') {
                log_stream(out,
                           "IP:" << _sta_ip->getStringValue() << " GW:" << _sta_gateway->getStringValue()
                                 << " MSK:" << _sta_netmask->getStringValue());
                return Error::Ok;
            }
            std::string gateway, netmask, ip;
            if (!(get_param(parameter, "GW", gateway) && get_param(parameter, "MSK", netmask) && get_param(parameter, "IP", ip))) {
                return Error::InvalidValue;
            }

            Error err = _sta_ip->setStringValue(ip);
            if (err == Error::Ok) {
                err = _sta_netmask->setStringValue(netmask);
            }
            if (err == Error::Ok) {
                err = _sta_gateway->setStringValue(gateway);
            }
            return err;
        }

        void wifi_stats(JSONencoder& j) {
            wifiImpl().addWifiStatsPrefix(j);
            auto mode = WiFi.getMode();
            if (mode != WIFI_OFF) {
                j.id_value_object("Available Size for LocalFS", formatBytes(localfs_size()));
                j.id_value_object("Web port", WebUI_Server::port());
                j.id_value_object("Data port", TelnetServer::port());
                j.id_value_object("Hostname", WiFi.getHostname());
            }

            switch (mode) {
                case WIFI_STA:

                    j.id_value_object("Current WiFi Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");

                    if (WiFi.isConnected()) {  //in theory no need but ...
                        j.id_value_object("Connected to", WiFi.SSID().c_str());
                        if (wifiImpl().allowRssiRead()) {
                            j.id_value_object("Signal", std::string("") + std::to_string(getSignal(WiFi.RSSI())) + "%");
                        }
                        wifiImpl().addStaPhyModeJson(j);
                        j.id_value_object("Channel", WiFi.channel());

                        j.id_value_object("IP Mode", _sta_mode->getStringValue());
                        j.id_value_object("IP", IP_string(WiFi.localIP()));
                        j.id_value_object("Gateway", IP_string(WiFi.gatewayIP()));
                        j.id_value_object("Mask", IP_string(WiFi.subnetMask()));
                        j.id_value_object("DNS", IP_string(WiFi.dnsIP()));

                    }  //this is web command so connection => no command
                    j.id_value_object("Disabled Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    break;
                case WIFI_AP:
                    j.id_value_object("Current WiFi Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    wifiImpl().addApDetailsJson(j);
                    j.id_value_object("IP", IP_string(WiFi.softAPIP()));

                    // Retrieving the configured gateway and netmask from the Arduino WiFi class
                    // is very tricky, so we just regurgitate the values that we passed in when
                    // starting the AP
                    j.id_value_object("Gateway", IP_string(WiFi.softAPIP()));
                    j.id_value_object("Mask", "255.255.255.0");

                    j.id_value_object("Disabled Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                    break;
                case WIFI_AP_STA:  //we should not be in this state but just in case ....
                    j.id_value_object("Mixed", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                    j.id_value_object("Mixed", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    break;
                default:  //we should not be there if no wifi ....

                    j.id_value_object("Current WiFi Mode", "Off");
                    break;
            }
        }

        static void reportStatus(Channel& out) {
            wifiImpl().addStatusPrefix(out);
            auto mode = WiFi.getMode();
            if (mode != WIFI_OFF) {
                log_stream(out, "Available Size for LocalFS: " << formatBytes(localfs_size()));
                log_stream(out, "Web port: " << WebUI_Server::port());
                log_stream(out, "Hostname: " << WiFi.getHostname());
            }

            switch (mode) {
                case WIFI_STA:
                    print_mac(out, "Current WiFi Mode: STA", WiFi.macAddress().c_str());

                    if (WiFi.isConnected()) {  //in theory no need but ...
                        log_stream(out, "Connected to: " << WiFi.SSID().c_str());
                        if (wifiImpl().allowRssiRead()) {
                            log_stream(out, "Signal: " << getSignal(WiFi.RSSI()) << "%");
                        }
                        wifiImpl().addStaPhyModeStatus(out);
                        log_stream(out, "Channel: " << WiFi.channel());

                        log_stream(out, "IP Mode: " << _sta_mode->getStringValue());
                        log_stream(out, "IP: " << IP_string(WiFi.localIP()));
                        log_stream(out, "Gateway: " << IP_string(WiFi.gatewayIP()));
                        log_stream(out, "Mask: " << IP_string(WiFi.subnetMask()));
                        log_stream(out, "DNS: " << IP_string(WiFi.dnsIP()));

                    }  //this is web command so connection => no command
                    print_mac(out, "Disabled Mode: AP", WiFi.softAPmacAddress().c_str());
                    break;
                case WIFI_AP:
                    print_mac(out, "Current WiFi Mode: AP", WiFi.softAPmacAddress().c_str());
                    wifiImpl().addApDetailsStatus(out);

                    log_stream(out, "IP: " << IP_string(WiFi.softAPIP()));

                    // Retrieving the configured gateway and netmask from the Arduino WiFi class
                    // is very tricky, so we just regurgitate the values that we passed in when
                    // starting the AP
                    log_stream(out, "Gateway: " << IP_string(IPAddress(WiFi.softAPIP())));
                    log_stream(out, "Mask: 255.255.255.0");

                    print_mac(out, "Disabled Mode: STA", WiFi.macAddress().c_str());
                    break;
                case WIFI_AP_STA:  //we should not be in this state but just in case ....
                    log_string(out, "");

                    print_mac(out, "Mixed: STA", WiFi.macAddress().c_str());
                    print_mac(out, "Mixed: AP", WiFi.softAPmacAddress().c_str());
                    break;
                default:  //we should not be there if no wifi ....

                    log_string(out, "Current WiFi Mode: Off");
                    break;
            }

            LogStream s(out, "Notifications: ");
            s << (NotificationsService::started() ? "Enabled" : "Disabled");
            if (NotificationsService::started()) {
                s << "(" << NotificationsService::getTypeString() << ")";
            }
        }

        void status_report(Channel& out) { reportStatus(out); }

        static Error showWiFiStatus(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            (void)parameter;
            (void)auth_level;
            reportStatus(out);
            return Error::Ok;
        }

        static const char* modeName() {
            switch (WiFi.getMode()) {
                case WIFI_OFF:
                    return "None";
                case WIFI_STA:
                    return "STA";
                case WIFI_AP:
                    return "AP";
                default:
                    return "?";
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

                switch (WiFi.getMode()) {
                    case WIFI_AP:
                        j.member("WebSocketIP", IP_string(WiFi.softAPIP()));
                        break;
                    case WIFI_STA:
                        j.member("WebSocketIP", IP_string(WiFi.localIP()));
                        break;
                    case WIFI_AP_STA:
                        j.member("WebSocketIP", IP_string(WiFi.softAPIP()));
                        break;
                    default:
                        j.member("WebSocketIP", "0.0.0.0");
                        break;
                }

                j.member("WebSocketPort", std::to_string(WebUI_Server::port()));
                j.member("HostName", WiFi.getHostname());
                j.member("WiFiMode", modeName());
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
            switch (WiFi.getMode()) {
                case WIFI_AP:
                    s << IP_string(WiFi.softAPIP());
                    break;
                case WIFI_STA:
                    s << IP_string(WiFi.localIP());
                    break;
                case WIFI_AP_STA:
                    s << IP_string(WiFi.softAPIP());
                    break;
                default:
                    s << "0.0.0.0";
                    break;
            }
#endif
            s << " # hostname:";
            s << WiFi.getHostname();
            if (WiFi.getMode() == WIFI_AP) {
                s << "(AP mode)";
            }

            //to save time in decoding `?`
            s << " # axis:" << Axes::_numberAxis;
            return Error::Ok;
        }

        static int32_t getSignal(int32_t RSSI) {
            if (RSSI <= -100) {
                return 0;
            }
            if (RSSI >= -50) {
                return 100;
            }
            return 2 * (RSSI + 100);
        }

        static bool ConnectSTA2AP() {
            std::string msg, msg_out;
            uint8_t     dot = 0;
            bool        use_dhcp = (_sta_mode && (_sta_mode->get() == DHCP_MODE));
            size_t      max_attempts = use_dhcp ? 20 : 10;
            for (size_t i = 0; i < max_attempts; ++i) {
                msg.clear();
                auto ret = WiFi.status();
                log_info("STA connect attempt " << (i + 1) << "/" << max_attempts << " status=" << static_cast<int>(ret));
                switch (ret) {
                    case WL_NO_SSID_AVAIL:
                        log_info("No SSID");
                        return false;
                    case WL_CONNECT_FAILED:
                        if (i < (max_attempts - 3)) {
                            log_info("Connection failed (transient), re-trying STA start");
                            WiFi.disconnect();
                            delay_ms(200);
                            const char* retry_password = _sta_password->get();
                            wifiImpl().beginSta(_sta_ssid->get(), (strlen(retry_password) > 0) ? retry_password : NULL, nullptr);
                            break;
                        }
                        log_info("Connection failed");
                        return false;
                    case WL_CONNECTED:
                        if (use_dhcp && WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
                            log_info("Connected, waiting for DHCP lease");
                            break;
                        }
                        log_info("Connected - IP is " << IP_string(WiFi.localIP()));
                        return true;
                    case WL_DISCONNECTED:
                        if (use_dhcp && i < (max_attempts - 3)) {
                            log_info("Disconnected (transient during DHCP/association), retrying");
                            break;
                        }
                        log_info("Disconnected");
                        return false;
                    case 0x82:
                        log_info("No Net");
                        return false;
                    case 0x83:
                        if (use_dhcp && i < (max_attempts - 5)) {
                            log_info("Bad authentication reported (transient), retrying");
                            break;
                        }
                        log_info("Bad authentication");
                        return false;
                    default:
                        log_info("The problem was " << ret);
                        if ((dot > 3) || (dot == 0)) {
                            dot     = 0;
                            msg_out = "Connecting";
                        }
                        msg_out += ".";
                        msg = msg_out;
                        dot++;
                        break;
                }
                if (!msg.empty()) {
                    log_info(msg);
                }
                delay_ms(2000);  // Give it some time to connect
            }
            return false;
        }

        static bool StartSTA() {
            //Sanity check
            auto mode = WiFi.getMode();
            if (mode == WIFI_STA || mode == WIFI_AP_STA) {
                WiFi.disconnect();
            }

            if (mode == WIFI_AP || mode == WIFI_AP_STA) {
                WiFi.softAPdisconnect();
            }

            //SSID
            const char* SSID = _sta_ssid->get();
            if (strlen(SSID) == 0) {
                log_info("STA SSID is not set");
                return false;
            }
            //Hostname needs to be set before mode to take effect
            log_info("Hostname is " << _hostname->get());
            WiFi.setHostname(_hostname->get());
            WiFi.mode(WIFI_STA);
            wifiImpl().prepareStartSta(_sta_min_security->get(), _fast_scan->get(), _ap_country->getStringValue());
            //Get parameters for STA
            //password
            const char* password = _sta_password->get();
            log_info("STA password length is " << strlen(password));
            int8_t      IP_mode  = _sta_mode->get();
            int32_t     IP       = _sta_ip->get();
            int32_t     GW       = _sta_gateway->get();
            int32_t     MK       = _sta_netmask->get();
            //if not DHCP
            if (IP_mode != DHCP_MODE) {
                IPAddress ip((uint32_t)IP), mask((uint32_t)MK), gateway((uint32_t)GW);
                WiFi.config(ip, gateway, mask);
            }

            uint8_t        selected_bssid[6];
            const uint8_t* bssid     = nullptr;
            bool           use_bssid = false;
            if (!wifiImpl().getStaBssidForSecurity(SSID, _sta_min_security->get(), selected_bssid, use_bssid)) {
                log_error("No AP found for SSID " << SSID << " meeting minimum security requirement");
                return false;
            }
            if (use_bssid) {
                bssid = selected_bssid;
            }

            log_info("Connecting to STA SSID:" << SSID);
            if (wifiImpl().beginSta(SSID, (strlen(password) > 0) ? password : NULL, bssid)) {
                return ConnectSTA2AP();
            } else {
                log_info("Starting client failed");
                return false;
            }
        }

        static bool StartAP() {
            //Sanity check
            if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.disconnect();
            }
            if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.softAPdisconnect();
            }

            WiFi.mode(WIFI_AP);
            wifiImpl().prepareStartAp(_ap_country ? _ap_country->getStringValue() : nullptr);

            //Get parameters for AP
            const char* SSID = _ap_ssid->get();

            const char* password = _ap_password->get();

            int8_t channel = int8_t(_ap_channel->get());

            IPAddress ip(_ap_ip->get());
            IPAddress mask;
            mask.fromString("255.255.255.0");

            log_info("AP SSID " << SSID << " IP " << IP_string(ip) << " mask " << IP_string(mask) << " channel " << channel);

            //Set static IP
            WiFi.softAPConfig(ip, ip, mask);

            //Start AP
            if (WiFi.softAP(SSID, (strlen(password) > 0) ? password : NULL, channel)) {
                log_info("AP started");
                return true;
            }

            log_info("AP did not start");
            return false;
        }

        static void reset() {
            WiFi.persistent(false);
            WiFi.disconnect(true);
            wifiImpl().onWifiOff();
            WiFi.mode(WIFI_OFF);
        }

        static void StopWiFi() {
            if (WiFi.getMode() != WIFI_OFF) {
                if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
                    WiFi.disconnect(true);
                }
                if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
                    WiFi.softAPdisconnect(true);
                }
                wifiImpl().onWifiOff();
                WiFi.mode(WIFI_OFF);
            }
            log_info("WiFi Off");
        }

        static std::string station_info() {
            std::string result;

            auto mode = WiFi.getMode();
            if (mode == WIFI_STA || mode == WIFI_AP_STA) {
                result += "Mode=STA:SSID=";
                result += WiFi.SSID().c_str();
                result += ":Status=";
                result += (WiFi.status() == WL_CONNECTED) ? "Connected" : "Not connected";
                result += ":IP=";
                result += IP_string(WiFi.localIP());
                result += ":MAC=";
                std::string mac(WiFi.macAddress().c_str());
                std::replace(mac.begin(), mac.end(), ':', '-');
                result += mac;
            }
            return result;
        }

        static std::string ap_info() {
            std::string result;

            auto mode = WiFi.getMode();
            if (mode == WIFI_AP || mode == WIFI_AP_STA) {
                if (WiFi.getMode() == WIFI_AP_STA) {
                    result += "]\n[MSG:";
                }
                result += wifiImpl().apInfoString();
            }
            return result;
        }

        static bool isOn() { return !(WiFi.getMode() == WIFI_OFF); }

        // Used by js/scanwifidlg.js

        static Error listAPs(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP410
            (void)auth_level;
            JSONencoder j(&out);
            j.begin();

            if (parameter != NULL && (strstr(parameter, "json=yes")) != NULL) {
                j.member("cmd", "410");
                j.member("status", "ok");
                j.begin_array("data");
            } else {
                j.begin_array("AP_LIST");
            }

            int32_t n = wifiImpl().beginApListScan();

            for (int i = 0; i < n; ++i) {
                j.begin_object();
#ifdef ARDUINO_ARCH_RP2040
                j.member("SSID", WiFi.SSID(i));
#else
                j.member("SSID", WiFi.SSID(i).c_str());
#endif
                j.member("SIGNAL", getSignal(WiFi.RSSI(i)));
                j.member("IS_PROTECTED", wifiImpl().isApProtected(i));
                j.end_object();
            }
            wifiImpl().finishApListScan();
            j.end_array();
            j.end();
            return Error::Ok;
        }

    public:
        WiFiConfig(const char* name) : Module(name) {}

        void init() {
            _sta_ssid    = new StringSetting("Station SSID", WEBSET, WA, "ESP100", "Sta/SSID", "", MIN_SSID_LENGTH, MAX_SSID_LENGTH);
            _hostname    = new HostnameSetting("Hostname", "ESP112", "Hostname", "fluidnc");
            _ap_channel  = new IntSetting("AP Channel", WEBSET, WA, "ESP108", "AP/Channel", 1, 1, 14);
            _ap_ip       = new IPaddrSetting("AP Static IP", WEBSET, WA, "ESP107", "AP/IP", "192.168.0.1");
            _ap_password = new PasswordSetting("AP Password", "ESP106", "AP/Password", "12345678");
            _ap_ssid     = new StringSetting("AP SSID", WEBSET, WA, "ESP105", "AP/SSID", "FluidNC", MIN_SSID_LENGTH, MAX_SSID_LENGTH);
            _ap_country = new EnumSetting(
                "AP regulatory domain", WEBSET, WA, NULL, "AP/Country", getWifiCountryDefault(), getWifiCountryOptions());
            _sta_netmask = new IPaddrSetting("Station Static Mask", WEBSET, WA, NULL, "Sta/Netmask", NULL_IP);
            _sta_gateway = new IPaddrSetting("Station Static Gateway", WEBSET, WA, NULL, "Sta/Gateway", NULL_IP);
            _sta_ip      = new IPaddrSetting("Station Static IP", WEBSET, WA, NULL, "Sta/IP", NULL_IP);
            _sta_mode    = new EnumSetting("Station IP Mode", WEBSET, WA, "ESP102", "Sta/IPMode", DHCP_MODE, &staModeOptions);
            _fast_scan   = new EnumSetting("WiFi Fast Scan", WEBSET, WA, NULL, "WiFi/FastScan", 0, &onoffOptions);
            _sta_min_security = new EnumSetting("Station Security",
                                                WEBSET,
                                                WA,
                                                NULL,
                                                "Sta/MinSecurity",
                                                wifiImpl().staSecurityDefault(),
                                                wifiImpl().staSecurityOptions());
            _sta_password = new PasswordSetting("Station Password", "ESP101", "Sta/Password", "");

            _mode = new EnumSetting("WiFi mode", WEBSET, WA, "ESP116", "WiFi/Mode", WiFiFallback, &wifiModeOptions);
            if (wifiImpl().supportsPsMode()) {
                _wifi_ps_mode = new EnumSetting("WiFi power saving mode",
                                                WEBSET,
                                                WA,
                                                NULL,
                                                "WiFi/PsMode",
                                                wifiImpl().psModeDefault(),
                                                wifiImpl().psModeOptions());
            } else {
                _wifi_ps_mode = nullptr;
            }

            new WebCommand(NULL, WEBCMD, WU, "ESP410", "WiFi/ListAPs", listAPs);
            new WebCommand(NULL, WEBCMD, WG, NULL, "wifi/status", showWiFiStatus, anyState);
            new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState);

            new WebCommand(NULL, WEBCMD, WG, "ESP111", "System/IP", showIP);
            new WebCommand("IP=ipaddress MSK=netmask GW=gateway", WEBCMD, WA, "ESP103", "Sta/Setup", showSetStaParams);

            //stop active services
            // wifi_services.end();

            switch (_mode->get()) {
                case WiFiOff:
                    log_info("WiFi is disabled");
                    return;
                case WiFiSTA:
                    if (StartSTA()) {
                        goto wifi_on;
                    }
                    goto wifi_off;
                case WiFiFallback:
                    if (StartSTA()) {
                        goto wifi_on;
                    } else {  // STA failed, reset
                        WiFi.mode(WIFI_OFF);
                        wifiImpl().onStaFallbackFailure();
                        delay_ms(100);
                    }
                    // fall through to fallback to AP mode
                    [[fallthrough]];
                case WiFiAP:
                    if (StartAP()) {
                        goto wifi_on;
                    }
                    goto wifi_off;
            }

        wifi_off:
            log_info("WiFi off");
            WiFi.mode(WIFI_OFF);
            return;

        wifi_on:
            wifiImpl().onWifiOn(_wifi_ps_mode ? _wifi_ps_mode->get() : 0);
            log_info("WiFi on");
            //        wifi_services.begin();
        }

        void deinit() override { StopWiFi(); }

        void build_info(Channel& channel) {
            std::string sti = station_info();
            if (sti.length()) {
                log_msg_to(channel, sti);
            }
            std::string api = ap_info();
            if (api.length()) {
                log_msg_to(channel, api);
            }
            if (!sti.length() && !api.length()) {
                log_msg_to(channel, "No Wifi");
            }
        }

        void poll() {
            wifiImpl().poll();
        }

        bool is_radio() override { return true; }

        ~WiFiConfig() { deinit(); }
    };

    ModuleFactory::InstanceBuilder<WiFiConfig> __attribute__((init_priority(105))) wifi_module("wifi", true);
}
