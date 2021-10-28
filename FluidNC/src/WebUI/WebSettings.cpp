// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  WebSettings.cpp - Settings and Commands for the interface
  to ESP3D_WebUI.  Code snippets extracted from commands.cpp in the
  old WebUI interface code are presented via the Settings class.
*/

#include "WebSettings.h"

#include "../Settings.h"
#include "../Machine/MachineConfig.h"
#include "../Machine/WifiSTAConfig.h"
#include "../Configuration/JsonGenerator.h"
#include "../Uart.h"    // Uart0.baud
#include "../Report.h"  // git_info

#include "Commands.h"  // COMMANDS::wait(1);
#include "WifiConfig.h"
#include "WebClient.h"
#include "WebServer.h"
#include "NotificationsService.h"  // notificationsservice
#include "TelnetServer.h"          // telnet_server

#include <cstring>

#ifdef ENABLE_WIFI
#    include <WiFi.h>
#    include <esp_wifi.h>
#    include <esp_ota_ops.h>
#endif

#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

namespace WebUI {

    enum_opt_t onoffOptions = { { "OFF", 0 }, { "ON", 1 } };

#ifdef ENABLE_WIFI
    enum_opt_t wifiModeOptions = {
        { "Off", WiFiOff },
        { "STA", WiFiSTA },
        { "AP", WiFiAP },
        { "STA>AP", WiFiFallback },
    };
    EnumSetting* wifi_mode;

    StringSetting* wifi_sta_ssid;
    StringSetting* wifi_sta_password;

    EnumSetting*   wifi_sta_mode;
    IPaddrSetting* wifi_sta_ip;
    IPaddrSetting* wifi_sta_gateway;
    IPaddrSetting* wifi_sta_netmask;

    StringSetting* wifi_ap_ssid;
    StringSetting* wifi_ap_password;

    IPaddrSetting* wifi_ap_ip;

    IntSetting* wifi_ap_channel;

    StringSetting* wifi_hostname;
    EnumSetting*   http_enable;
    IntSetting*    http_port;
    EnumSetting*   telnet_enable;
    IntSetting*    telnet_port;

    enum_opt_t staModeOptions = {
        { "DHCP", DHCP_MODE },
        { "Static", STATIC_MODE },
    };
#endif

#ifdef ENABLE_BLUETOOTH
    EnumSetting*   bt_enable;
    StringSetting* bt_name;
#endif

    Print* webresponse = nullptr;
    bool   isWeb       = false;

    typedef struct {
        char* key;
        char* value;
    } keyval_t;

    static keyval_t params[10];
    bool            split_params(char* parameter) {
        int i = 0;
        for (char* s = parameter; *s; s++) {
            if (*s == '=') {
                params[i].value = s + 1;
                *s              = '\0';
                // Search backward looking for the start of the key,
                // either just after a space or at the beginning of the strin
                if (s == parameter) {
                    return false;
                }
                for (char* k = s - 1; k >= parameter; --k) {
                    if (*k == '\0') {
                        // If we find a NUL - i.e. the end of the previous key -
                        // before finding a space, the string is malformed.
                        return false;
                    }
                    if (*k == ' ') {
                        *k              = '\0';
                        params[i++].key = k + 1;
                        break;
                    }
                    if (k == parameter) {
                        params[i++].key = k;
                    }
                }
            }
        }
        params[i].key = NULL;
        return true;
    }

    char  nullstr[1] = { '\0' };
    char* get_param(const char* key, bool allowSpaces) {
        for (keyval_t* p = params; p->key; p++) {
            if (!strcasecmp(key, p->key)) {
                if (!allowSpaces) {
                    for (char* s = p->value; *s; s++) {
                        if (*s == ' ') {
                            *s = '\0';
                            break;
                        }
                    }
                }
                return p->value;
            }
        }
        return nullstr;
    }
}

Error WebCommand::action(char* value, WebUI::AuthenticationLevel auth_level, Print& out) {
    if (_cmdChecker && _cmdChecker()) {
        return Error::AnotherInterfaceBusy;
    }
    char empty = '\0';
    if (!value) {
        value = &empty;
    }
    WebUI::webresponse = &out;
    return _action(value, auth_level);
};

namespace WebUI {
    static int webColumn = 0;
    // We create a variety of print functions to make the rest
    // of the code more compact and readable.
    static void webPrint(const char* s) {
        *webresponse << s;
        webColumn += strlen(s);
    }
    static void webPrintSetColumn(int column) {
        while (webColumn < column) {
            webPrint(" ");
        }
    }
    static void webPrint(String s) { webPrint(s.c_str()); }
    static void webPrint(const char* s1, const char* s2) {
        webPrint(s1);
        webPrint(s2);
    }
    static void webPrint(const char* s1, String s2) {
        webPrint(s1);
        webPrint(s2.c_str());
    }
    static void webPrint(const char* s1, const char* s2, const char* s3) {
        webPrint(s1);
        webPrint(s2);
        webPrint(s3);
    }
    static void webPrint(const char* s, IPAddress ip) {
        webPrint(s);
        webPrint(ip.toString().c_str());
    }
    static void webPrintln(const char* s) {
        webPrint(s);
        webPrint("\n");
        webColumn = 0;
    }
    static void webPrintln(String s) { webPrintln(s.c_str()); }
    static void webPrintln(const char* s1, const char* s2) {
        webPrint(s1);
        webPrintln(s2);
    }
    static void webPrintln(const char* s, IPAddress ip) {
        webPrint(s);
        webPrintln(ip.toString().c_str());
    }
    static void webPrintln(const char* s, String s2) { webPrintln(s, s2.c_str()); }

    static void print_mac(const char* s, String mac) {
        webPrint(s);
        webPrint(" (");
        webPrint(mac);
        webPrintln(")");
    }

    static Error showFwInfo(char* parameter, AuthenticationLevel auth_level) {  // ESP800
        webPrint("FW version: FluidNC ");
        webPrint(git_info);
        // TODO: change grbl-embedded to FluidNC after fixing WebUI
        webPrint(" # FW target:grbl-embedded  # FW HW:");
        webPrint(config->_sdCard->get_state() == SDCard::State::NotPresent ? "No SD" : "Direct SD");
        webPrint("  # primary sd:/sd # secondary sd:none # authentication:");
#ifdef ENABLE_AUTHENTICATION
        webPrint("yes");
#else
        webPrint("no");
#endif
#ifdef ENABLE_WIFI
        webPrint(" # webcommunication: Sync: ", String(web_server.port() + 1));
        webPrint(":");
        switch (WiFi.getMode()) {
            case WIFI_MODE_AP:
                webPrint(WiFi.softAPIP().toString());
                break;
            case WIFI_MODE_STA:
                webPrint(WiFi.localIP().toString());
                break;
            case WIFI_MODE_APSTA:
                webPrint(WiFi.softAPIP().toString());
                break;
            default:
                webPrint("0.0.0.0");
                break;
        }
        webPrint(" # hostname:", wifi_config.Hostname());
        if (WiFi.getMode() == WIFI_AP) {
            webPrint("(AP mode)");
        }
#endif
        //to save time in decoding `?`
        webPrintln(" # axis:", String(config->_axes->_numberAxis));
        return Error::Ok;
    }

    static Error SPIFFSSize(char* parameter, AuthenticationLevel auth_level) {  // ESP720
        webPrint(parameter);
        webPrint("SPIFFS  Total:", formatBytes(SPIFFS.totalBytes()));
        webPrintln(" Used:", formatBytes(SPIFFS.usedBytes()));
        return Error::Ok;
    }

    static Error formatSpiffs(char* parameter, AuthenticationLevel auth_level) {  // ESP710
        if (strcmp(parameter, "FORMAT") != 0) {
            webPrintln("Parameter must be FORMAT");
            return Error::InvalidValue;
        }
        webPrint("Formatting");
        SPIFFS.format();
        webPrintln("...Done");
        return Error::Ok;
    }

    static Error runLocalFile(char* parameter, AuthenticationLevel auth_level) {  // ESP700
        if (sys.state != State::Idle) {
            webPrintln("Busy");
            return Error::IdleError;
        }
        String path = trim(parameter);
        if ((path.length() > 0) && (path[0] != '/')) {
            path = "/" + path;
        }
        if (!SPIFFS.exists(path)) {
            webPrintln("Error: No such file!");
            return Error::FsFileNotFound;
        }
        File currentfile = SPIFFS.open(path, FILE_READ);
        if (!currentfile) {  //if file open success
            return Error::FsFailedOpenFile;
        }
        //until no line in file
        Error  err;
        Error  accumErr = Error::Ok;
        Print& out      = webresponse ? *webresponse : allClients;
        while (currentfile.available()) {
            String currentline = currentfile.readStringUntil('\n');
            if (currentline.length() > 0) {
                uint8_t line[256];
                currentline.getBytes(line, 255);
                err = execute_line((char*)line, out, auth_level);
                if (err != Error::Ok) {
                    accumErr = err;
                }
                COMMANDS::wait(1);
            }
        }
        currentfile.close();
        return accumErr;
    }

    static Error showLocalFile(char* parameter, AuthenticationLevel auth_level) {  // ESP701
        if (notIdleOrAlarm()) {
            return Error::IdleError;
        }
        String path = trim(parameter);
        if ((path.length() > 0) && (path[0] != '/')) {
            path = "/" + path;
        }
        if (!SPIFFS.exists(path)) {
            webPrintln("Error: No such file!");
            return Error::FsFileNotFound;
        }
        File currentfile = SPIFFS.open(path, FILE_READ);
        if (!currentfile) {
            return Error::FsFailedOpenFile;
        }
        webPrint(dataBeginMarker);
        while (currentfile.available()) {
            // String currentline = currentfile.readStringUntil('\n');
            //            if (currentline.length() > 0) {
            //                webPrintln(currentline);
            //            }
            webPrintln(currentfile.readStringUntil('\n'));
        }
        currentfile.close();
        webPrint(dataEndMarker);
        return Error::Ok;
    }

#ifdef ENABLE_WIFI
    enum_opt_t notificationOptions = {
        { "NONE", 0 },
        { "LINE", 3 },
        { "PUSHOVER", 1 },
        { "EMAIL", 2 },
    };
    EnumSetting*   notification_type;
    StringSetting* notification_t1;
    StringSetting* notification_t2;
    StringSetting* notification_ts;

    static Error showSetNotification(char* parameter, AuthenticationLevel auth_level) {  // ESP610
        if (*parameter == '\0') {
            webPrint("", notification_type->getStringValue());
            webPrintln(" ", notification_ts->getStringValue());
            return Error::Ok;
        }
        if (!split_params(parameter)) {
            return Error::InvalidValue;
        }
        char* ts  = get_param("TS", false);
        char* t2  = get_param("T2", false);
        char* t1  = get_param("T1", false);
        char* ty  = get_param("type", false);
        Error err = notification_type->setStringValue(ty);
        if (err == Error::Ok) {
            err = notification_t1->setStringValue(t1);
        }
        if (err == Error::Ok) {
            err = notification_t2->setStringValue(t2);
        }
        if (err == Error::Ok) {
            err = notification_ts->setStringValue(ts);
        }
        return err;
    }

    static Error sendMessage(char* parameter, AuthenticationLevel auth_level) {  // ESP600
        if (*parameter == '\0') {
            webPrintln("Invalid message!");
            return Error::InvalidValue;
        }
        if (!notificationsservice.sendMSG("GRBL Notification", parameter)) {
            webPrintln("Cannot send message!");
            return Error::MessageFailed;
        }
        return Error::Ok;
    }
#endif

#ifdef ENABLE_AUTHENTICATION
    static Error setUserPassword(char* parameter, AuthenticationLevel auth_level) {  // ESP555
        if (*parameter == '\0') {
            user_password->setDefault();
            return Error::Ok;
        }
        if (user_password->setStringValue(parameter) != Error::Ok) {
            webPrintln("Invalid Password");
            return Error::InvalidValue;
        }
        return Error::Ok;
    }
#endif

    static Error setSystemMode(char* parameter, AuthenticationLevel auth_level) {  // ESP444
        parameter = trim(parameter);
        if (strcasecmp(parameter, "RESTART") != 0) {
            webPrintln("Incorrect command");
            return Error::InvalidValue;
        }
        log_info("Restarting");
        COMMANDS::restart_ESP();
        return Error::Ok;
    }

    static Error restart(char* parameter, AuthenticationLevel auth_level) {
        parameter = trim(parameter);
        log_info("Restarting");
        COMMANDS::restart_ESP();
        return Error::Ok;
    }

    static void showWifiStats() {
#ifdef ENABLE_WIFI
        int mode = WiFi.getMode();
        if (mode != WIFI_MODE_NULL) {
            //Is OTA available ?
            size_t flashsize = 0;
            if (esp_ota_get_running_partition()) {
                const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                if (partition) {
                    flashsize = partition->size;
                }
            }
            webPrintln("Available Size for update: ", formatBytes(flashsize));
            webPrintln("Available Size for SPIFFS: ", formatBytes(SPIFFS.totalBytes()));

            webPrintln("Web port: ", String(web_server.port()));
            webPrintln("Data port: ", String(telnet_server.port()));
            webPrintln("Hostname: ", wifi_config.Hostname());
        }

        webPrint("Current WiFi Mode: ");
        switch (mode) {
            case WIFI_STA:
                print_mac("STA", WiFi.macAddress());

                webPrint("Connected to: ");
                if (WiFi.isConnected()) {  //in theory no need but ...
                    webPrintln(WiFi.SSID());
                    webPrintln("Signal: ", String(wifi_config.getSignal(WiFi.RSSI())) + "%");

                    uint8_t PhyMode;
                    esp_wifi_get_protocol(WIFI_IF_STA, &PhyMode);
                    const char* modeName;
                    switch (PhyMode) {
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N:
                            modeName = "11n";
                            break;
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G:
                            modeName = "11g";
                            break;
                        case WIFI_PROTOCOL_11B:
                            modeName = "11b";
                            break;
                        default:
                            modeName = "???";
                    }
                    webPrintln("Phy Mode: ", modeName);

                    webPrintln("Channel: ", String(WiFi.channel()));

                    tcpip_adapter_dhcp_status_t dhcp_status;
                    tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &dhcp_status);
                    webPrintln("IP Mode: ", dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "DHCP" : "Static");
                    webPrintln("IP: ", WiFi.localIP());
                    webPrintln("Gateway: ", WiFi.gatewayIP());
                    webPrintln("Mask: ", WiFi.subnetMask());
                    webPrintln("DNS: ", WiFi.dnsIP());

                }  //this is web command so connection => no command
                webPrint("Disabled Mode: ");
                print_mac("AP", WiFi.softAPmacAddress());
                break;
            case WIFI_AP:
                print_mac("AP", WiFi.softAPmacAddress());

                wifi_config_t conf;
                esp_wifi_get_config(WIFI_IF_AP, &conf);
                webPrintln("SSID: ", (const char*)conf.ap.ssid);
                webPrintln("Visible: ", (conf.ap.ssid_hidden == 0) ? "Yes" : "No");

                const char* mode;
                switch (conf.ap.authmode) {
                    case WIFI_AUTH_OPEN:
                        mode = "None";
                        break;
                    case WIFI_AUTH_WEP:
                        mode = "WEP";
                        break;
                    case WIFI_AUTH_WPA_PSK:
                        mode = "WPA";
                        break;
                    case WIFI_AUTH_WPA2_PSK:
                        mode = "WPA2";
                        break;
                    default:
                        mode = "WPA/WPA2";
                }

                webPrintln("Authentication: ", mode);
                webPrintln("Max Connections: ", String(conf.ap.max_connection));

                tcpip_adapter_dhcp_status_t dhcp_status;
                tcpip_adapter_dhcps_get_status(TCPIP_ADAPTER_IF_AP, &dhcp_status);
                webPrintln("DHCP Server: ", dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "Started" : "Stopped");

                webPrintln("IP: ", WiFi.softAPIP());

                tcpip_adapter_ip_info_t ip_AP;
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_AP);
                webPrintln("Gateway: ", IPAddress(ip_AP.gw.addr));
                webPrintln("Mask: ", IPAddress(ip_AP.netmask.addr));

                wifi_sta_list_t          station;
                tcpip_adapter_sta_list_t tcpip_sta_list;
                esp_wifi_ap_get_sta_list(&station);
                tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                webPrintln("Connected clients: ", String(station.num));

                for (int i = 0; i < station.num; i++) {
                    webPrint(wifi_config.mac2str(tcpip_sta_list.sta[i].mac));
                    webPrintln(" ", IPAddress(tcpip_sta_list.sta[i].ip.addr));
                }
                webPrint("Disabled Mode: ");
                print_mac("STA", WiFi.macAddress());
                break;
            case WIFI_AP_STA:  //we should not be in this state but just in case ....
                webPrintln("Mixed");

                print_mac("STA", WiFi.macAddress());
                print_mac("AP", WiFi.softAPmacAddress());
                break;
            default:  //we should not be there if no wifi ....
                webPrintln("Off");
                break;
        }

        webPrint("Notifications: ");
        webPrint(notificationsservice.started() ? "Enabled" : "Disabled");
        if (notificationsservice.started()) {
            webPrint("(");
            webPrint(notificationsservice.getTypeString());
            webPrint(")");
        }
        webPrintln("");
#endif
    }

    static Error showSysStats(char* parameter, AuthenticationLevel auth_level) {  // ESP420
        webPrintln("Chip ID: ", String((uint16_t)(ESP.getEfuseMac() >> 32)));
        webPrintln("CPU Frequency: ", String(ESP.getCpuFreqMHz()) + "Mhz");
        webPrintln("CPU Temperature: ", String(temperatureRead(), 1) + "C");
        webPrintln("Free memory: ", formatBytes(ESP.getFreeHeap()));
        webPrintln("SDK: ", ESP.getSdkVersion());
        webPrintln("Flash Size: ", formatBytes(ESP.getFlashChipSize()));

#ifdef ENABLE_WIFI
        // Round baudRate to nearest 100 because ESP32 can say e.g. 115201
        webPrintln("Baud rate: ", String((Uart0.baud / 100) * 100));
        webPrintln("Sleep mode: ", WiFi.getSleep() ? "Modem" : "None");
        showWifiStats();
#endif

#ifdef ENABLE_BLUETOOTH
        webPrintln(bt_config.info());
#endif
        webPrint("FW version: FluidNC ");
        webPrint(git_info);
        webPrintln("");
        return Error::Ok;
    }

#ifdef ENABLE_WIFI
    //StringSetting* wifi_sta_password;
    //StringSetting* wifi_ap_password;

    static Error listAPs(char* parameter, AuthenticationLevel auth_level) {  // ESP410
        JSONencoder j(false, webresponse);
        j.begin();
        j.begin_array("AP_LIST");
        // An initial async scanNetworks was issued at startup, so there
        // is a good chance that scan information is already available.
        int n = WiFi.scanComplete();
        switch (n) {
            case -2:                      // Scan not triggered
                WiFi.scanNetworks(true);  // Begin async scan
                break;
            case -1:  // Scan in progress
                break;
            default:
                for (int i = 0; i < n; ++i) {
                    j.begin_object();
                    j.member("SSID", WiFi.SSID(i));
                    j.member("SIGNAL", wifi_config.getSignal(WiFi.RSSI(i)));
                    j.member("IS_PROTECTED", WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                    //            j->member("IS_PROTECTED", WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1");
                    j.end_object();
                }
                WiFi.scanDelete();
                // Restart the scan in async mode so new data will be available
                // when we ask again.
                n = WiFi.scanComplete();
                if (n == -2) {
                    WiFi.scanNetworks(true);
                }
                break;
        }
        j.end_array();
        j.end();
        return Error::Ok;
    }
#endif

    static Error setWebSetting(char* parameter, AuthenticationLevel auth_level) {  // ESP401
        // We do not need the "T=" (type) parameter because the
        // Setting objects know their own type
        if (!split_params(parameter)) {
            return Error::InvalidValue;
        }
        char*       sval = get_param("V", true);
        const char* spos = get_param("P", false);
        if (*spos == '\0') {
            webPrintln("Missing parameter");
            return Error::InvalidValue;
        }
        Error ret = do_command_or_setting(spos, sval, auth_level, *webresponse);
        return ret;
    }

    static Error listSettings(char* parameter, AuthenticationLevel auth_level) {  // ESP400
        JSONencoder j(false, webresponse);
        j.begin();
        j.begin_array("EEPROM");

        // NVS settings
        j.setCategory("nvs");
        for (Setting* js = Setting::List; js; js = js->next()) {
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

    static Error openSDFile(char* parameter, Print& client, AuthenticationLevel auth_level) {
        if (*parameter == '\0') {
            webPrintln("Missing file name!");
            return Error::InvalidValue;
        }
        String path = trim(parameter);
        if (path[0] != '/') {
            path = "/" + path;
        }
        switch (config->_sdCard->begin(SDCard::State::BusyReading)) {
            case SDCard::State::Idle:
                break;
            case SDCard::State::NotPresent:
                webPrintln("No SD Card");
                return Error::FsFailedMount;
            default:
                webPrintln("SD Card Busy");
                return Error::FsFailedBusy;
        }

        if (!config->_sdCard->openFile(SD, path.c_str(), client, auth_level)) {
            report_status_message(Error::FsFailedRead, client);
            webPrintln("");
            return Error::FsFailedOpenFile;
        }
        return Error::Ok;
    }

    static Error showSDFile(char* parameter, AuthenticationLevel auth_level) {  // ESP221
        if (notIdleOrAlarm()) {
            return Error::IdleError;
        }
        Error  err;
        Print& client = (webresponse) ? *webresponse : allClients;
        if ((err = openSDFile(parameter, client, auth_level)) != Error::Ok) {
            return err;
        }
        webPrint(dataBeginMarker);
        char  fileLine[255];
        Error res;
        while ((res = config->_sdCard->readFileLine(fileLine, 255)) == Error::Ok) {
            webPrintln(fileLine);
        }
        if (res != Error::Eof) {
            webPrintln(errorString(res));
        }
        config->_sdCard->closeFile();
        webPrint(dataEndMarker);
        return Error::Ok;
    }

    static Error runSDFile(char* parameter, AuthenticationLevel auth_level) {  // ESP220
        Error err;
        if (sys.state == State::Alarm || sys.state == State::ConfigAlarm) {
            webPrintln("Alarm");
            return Error::IdleError;
        }
        if (sys.state != State::Idle) {
            webPrintln("Busy");
            return Error::IdleError;
        }
        Print& client = (webresponse) ? *webresponse : allClients;
        if ((err = openSDFile(parameter, client, auth_level)) != Error::Ok) {
            return err;
        }
        auto sdCard = config->_sdCard;

        char  fileLine[255];
        Error res = sdCard->readFileLine(fileLine, 255);
        if (res != Error::Ok) {
            report_status_message(res, client);
            // report_status_message will close the file
            webPrintln("");
            return Error::Ok;
        }
        // execute the first line now; Protocol.cpp handles later ones when sdCard._readyNext
        report_status_message(execute_line(fileLine, client, sdCard->getAuthLevel()), client);
        report_realtime_status(client);
        return Error::Ok;
    }

    static Error deleteObject(fs::FS fs, char* name) {
        name = trim(name);
        if (*name == '\0') {
            webPrintln("Missing file name!");
            return Error::InvalidValue;
        }
        String path = name;
        if (name[0] != '/') {
            path = "/" + path;
        }
        File file2del = fs.open(path);
        if (!file2del) {
            webPrintln("Cannot find file!");
            return Error::FsFileNotFound;
        }
        if (file2del.isDirectory()) {
            if (!fs.rmdir(path)) {
                webPrintln("Cannot delete directory! Is directory empty?");
                return Error::FsFailedDelDir;
            }
            webPrintln("Directory deleted.");
        } else {
            if (!fs.remove(path)) {
                webPrintln("Cannot delete file!");
                return Error::FsFailedDelFile;
            }
            webPrintln("File deleted.");
        }
        file2del.close();
        return Error::Ok;
    }

    static Error deleteSDObject(char* parameter, AuthenticationLevel auth_level) {  // ESP215
        auto state = config->_sdCard->begin(SDCard::State::BusyWriting);
        if (state != SDCard::State::Idle) {
            webPrintln((state == SDCard::State::NotPresent) ? "No SD card" : "Busy");
            return Error::Ok;
        }
        Error res = deleteObject(SD, parameter);
        config->_sdCard->end();
        return res;
    }

    static Error listSDFiles(char* parameter, AuthenticationLevel auth_level) {  // ESP210
        switch (config->_sdCard->begin(SDCard::State::BusyReading)) {
            case SDCard::State::Idle:
                break;
            case SDCard::State::NotPresent:
                webPrintln("No SD Card");
                return Error::FsFailedMount;
            default:
                webPrintln("SD Card Busy");
                return Error::FsFailedBusy;
        }

        webPrintln("");
        config->_sdCard->listDir(SD, "/", 10, *webresponse);
        String ssd = "[SD Free:" + formatBytes(SD.totalBytes() - SD.usedBytes());
        ssd += " Used:" + formatBytes(SD.usedBytes());
        ssd += " Total:" + formatBytes(SD.totalBytes());
        ssd += "]";
        webPrintln(ssd);
        config->_sdCard->end();
        return Error::Ok;
    }

    void listDirLocalFS(fs::FS fs, const char* dirname, size_t levels, Print& client) {
        //char temp_filename[128]; // to help filter by extension	TODO: 128 needs a definition based on something
        File root = fs.open(dirname);
        if (!root) {
            //FIXME: need proper error for FS and not usd sd one
            report_status_message(Error::FsFailedOpenDir, client);
            return;
        }
        if (!root.isDirectory()) {
            //FIXME: need proper error for FS and not usd sd one
            report_status_message(Error::FsDirNotFound, client);
            return;
        }
        File file = root.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                if (levels) {
                    listDirLocalFS(fs, file.name(), levels - 1, client);
                }
            } else {
                allClients << "[FILE:" << file.name() << "|SIZE:" << file.size() << "]\n";
            }
            file = root.openNextFile();
        }
    }

    static Error deleteLocalFile(char* parameter, AuthenticationLevel auth_level) { return deleteObject(SPIFFS, parameter); }

    static Error listLocalFiles(char* parameter, AuthenticationLevel auth_level) {  // No ESP command
        webPrintln("");
        listDirLocalFS(SPIFFS, "/", 10, *webresponse);
        String ssd = "[Local FS Free:" + formatBytes(SPIFFS.totalBytes() - SPIFFS.usedBytes());
        ssd += " Used:" + formatBytes(SPIFFS.usedBytes());
        ssd += " Total:" + formatBytes(SPIFFS.totalBytes());
        ssd += "]";
        webPrintln(ssd);
        return Error::Ok;
    }

    static void listDirJSON(fs::FS fs, const char* dirname, size_t levels, JSONencoder* j) {
        File root = fs.open(dirname);
        File file = root.openNextFile();
        while (file) {
            const char* tailName = strchr(file.name(), '/');
            tailName             = tailName ? tailName + 1 : file.name();
            if (file.isDirectory() && levels) {
                j->begin_array(tailName);
                listDirJSON(fs, file.name(), levels - 1, j);
                j->end_array();
            } else {
                j->begin_object();
                j->member("name", tailName);
                j->member("size", file.size());
                j->end_object();
            }
            file = root.openNextFile();
        }
    }

    static Error listLocalFilesJSON(char* parameter, AuthenticationLevel auth_level) {  // No ESP command
        JSONencoder j(false, webresponse);
        j.begin();
        j.begin_array("files");
        listDirJSON(SPIFFS, "/", 4, &j);
        j.end_array();
        j.member("total", SPIFFS.totalBytes());
        j.member("used", SPIFFS.usedBytes());
        j.member("occupation", String(100 * SPIFFS.usedBytes() / SPIFFS.totalBytes()));
        j.end();
        return Error::Ok;
    }

    static Error showSDStatus(char* parameter, AuthenticationLevel auth_level) {  // ESP200
        const char* resp = "No SD card";
        switch (config->_sdCard->begin(SDCard::State::BusyReading)) {
            case SDCard::State::Idle:
                resp = "SD card detected";
                config->_sdCard->end();
                break;
            case SDCard::State::NotPresent:
                resp = "No SD card";
                break;
            default:
                resp = "Busy";
        }
        webPrintln(resp);
        return Error::Ok;
    }

    static Error setRadioState(char* parameter, AuthenticationLevel auth_level) {  // ESP115
        parameter = trim(parameter);
        if (*parameter == '\0') {
            // Display the radio state
            bool on = false;
#ifdef ENABLE_WIFI
            if (WiFi.getMode() != WIFI_MODE_NULL) {
                on = true;
            }
#endif

#ifdef ENABLE_BLUETOOTH
            if (WebUI::bt_enable->get() == 1 && bt_config.Is_BT_on()) {
                on = true;
            }
#endif

            webPrintln(on ? "ON" : "OFF");
            return Error::Ok;
        }
        int8_t on = -1;
        if (strcasecmp(parameter, "ON") == 0) {
            on = 1;
        } else if (strcasecmp(parameter, "OFF") == 0) {
            on = 0;
        }
        if (on == -1) {
            webPrintln("only ON or OFF mode supported!");
            return Error::InvalidValue;
        }

        //Stop everything
#ifdef ENABLE_WIFI
        if (WiFi.getMode() != WIFI_MODE_NULL) {
            wifi_config.StopWiFi();
        }
#endif
#ifdef ENABLE_BLUETOOTH
        if (WebUI::bt_enable->get() == 1 && bt_config.Is_BT_on()) {
            bt_config.end();
        }
#endif

        //if On start proper service
        if (!on) {
            webPrintln("[MSG: Radio is Off]");
            return Error::Ok;
        }

#ifdef ENABLE_WIFI
        //On
        if (wifi_config.begin()) {
            return Error::Ok;
        }
#endif

#ifdef ENABLE_BLUETOOTH
        if (WebUI::bt_enable->get()) {
            bt_config.begin();
        }
#endif

        webPrintln("[MSG: Radio is Off]");
        return Error::Ok;
    }

#ifdef ENABLE_WIFI
    static Error showIP(char* parameter, AuthenticationLevel auth_level) {  // ESP111
        webPrintln(parameter, WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP());
        return Error::Ok;
    }

    static Error showSetStaParams(char* parameter, AuthenticationLevel auth_level) {  // ESP103
        if (*parameter == '\0') {
            webPrint("IP:", wifi_sta_ip->getStringValue());
            webPrint(" GW:", wifi_sta_gateway->getStringValue());
            webPrintln(" MSK:", wifi_sta_netmask->getStringValue());
            return Error::Ok;
        }
        if (!split_params(parameter)) {
            return Error::InvalidValue;
        }
        char* gateway = get_param("GW", false);
        char* netmask = get_param("MSK", false);
        char* ip      = get_param("IP", false);

        Error err = wifi_sta_ip->setStringValue(ip);
        if (err == Error::Ok) {
            err = wifi_sta_netmask->setStringValue(netmask);
        }
        if (err == Error::Ok) {
            err = wifi_sta_gateway->setStringValue(gateway);
        }
        return err;
    }
#endif

    static Error showWebHelp(char* parameter, AuthenticationLevel auth_level) {  // ESP0
        webPrintln("Persistent web settings - $name to show, $name=value to set");
        webPrintln("ESPname FullName         Description");
        webPrintln("------- --------         -----------");
        for (Setting* s = Setting::List; s; s = s->next()) {
            if (s->getType() == WEBSET) {
                if (s->getGrblName()) {
                    webPrint(" ", s->getGrblName());
                }
                webPrintSetColumn(8);
                webPrint(s->getName());
                webPrintSetColumn(25);
                webPrintln(s->getDescription());
            }
        }
        webPrintln("");
        webPrintln("Other web commands: $name to show, $name=value to set");
        webPrintln("ESPname FullName         Values");
        webPrintln("------- --------         ------");
        for (Command* cp = Command::List; cp; cp = cp->next()) {
            if (cp->getType() == WEBCMD) {
                if (cp->getGrblName()) {
                    webPrint(" ", cp->getGrblName());
                }
                webPrintSetColumn(8);
                webPrint(cp->getName());
                if (cp->getDescription()) {
                    webPrintSetColumn(25);
                    webPrintln(cp->getDescription());
                } else {
                    webPrintln("");
                }
            }
        }
        return Error::Ok;
    }

    void make_wifi_settings() {
#ifdef ENABLE_WIFI
        new WebCommand(
            "TYPE=NONE|PUSHOVER|EMAIL|LINE T1=token1 T2=token2 TS=settings", WEBCMD, WA, "ESP610", "Notification/Setup", showSetNotification);
        notification_ts = new StringSetting(
            "Notification Settings", WEBSET, WA, NULL, "Notification/TS", DEFAULT_TOKEN, 0, MAX_NOTIFICATION_SETTING_LENGTH, NULL);
        notification_t2   = new StringSetting("Notification Token 2",
                                            WEBSET,
                                            WA,
                                            NULL,
                                            "Notification/T2",
                                            DEFAULT_TOKEN,
                                            MIN_NOTIFICATION_TOKEN_LENGTH,
                                            MAX_NOTIFICATION_TOKEN_LENGTH,
                                            NULL);
        notification_t1   = new StringSetting("Notification Token 1",
                                            WEBSET,
                                            WA,
                                            NULL,
                                            "Notification/T1",
                                            DEFAULT_TOKEN,
                                            MIN_NOTIFICATION_TOKEN_LENGTH,
                                            MAX_NOTIFICATION_TOKEN_LENGTH,
                                            NULL);
        notification_type = new EnumSetting(
            "Notification type", WEBSET, WA, NULL, "Notification/Type", DEFAULT_NOTIFICATION_TYPE, &notificationOptions, NULL);
        new WebCommand("message", WEBCMD, WU, "ESP600", "Notification/Send", sendMessage);

        telnet_port = new IntSetting(
            "Telnet Port", WEBSET, WA, "ESP131", "Telnet/Port", DEFAULT_TELNETSERVER_PORT, MIN_TELNET_PORT, MAX_TELNET_PORT, NULL);
        telnet_enable = new EnumSetting("Telnet Enable", WEBSET, WA, "ESP130", "Telnet/Enable", DEFAULT_TELNET_STATE, &onoffOptions, NULL);

        http_port =
            new IntSetting("HTTP Port", WEBSET, WA, "ESP121", "HTTP/Port", DEFAULT_WEBSERVER_PORT, MIN_HTTP_PORT, MAX_HTTP_PORT, NULL);
        http_enable = new EnumSetting("HTTP Enable", WEBSET, WA, "ESP120", "HTTP/Enable", DEFAULT_HTTP_STATE, &onoffOptions, NULL);

        wifi_hostname = new StringSetting("Hostname",
                                          WEBSET,
                                          WA,
                                          "ESP112",
                                          "Hostname",
                                          DEFAULT_HOSTNAME,
                                          MIN_HOSTNAME_LENGTH,
                                          MAX_HOSTNAME_LENGTH,
                                          (bool (*)(char*))WiFiConfig::isHostnameValid);

        wifi_ap_channel =
            new IntSetting("AP Channel", WEBSET, WA, "ESP108", "AP/Channel", DEFAULT_AP_CHANNEL, MIN_CHANNEL, MAX_CHANNEL, NULL);
        wifi_ap_ip       = new IPaddrSetting("AP Static IP", WEBSET, WA, "ESP107", "AP/IP", DEFAULT_AP_IP, NULL);
        wifi_ap_password = new StringSetting("AP Password",
                                             WEBSET,
                                             WA,
                                             "ESP106",
                                             "AP/Password",
                                             DEFAULT_AP_PWD,
                                             MIN_PASSWORD_LENGTH,
                                             MAX_PASSWORD_LENGTH,
                                             (bool (*)(char*))WiFiConfig::isPasswordValid);
        wifi_ap_ssid     = new StringSetting(
            "AP SSID", WEBSET, WA, "ESP105", "AP/SSID", DEFAULT_AP_SSID, MIN_SSID_LENGTH, MAX_SSID_LENGTH, (bool (*)(char*))WiFiConfig::isSSIDValid);
        wifi_sta_netmask = new IPaddrSetting("Station Static Mask", WEBSET, WA, NULL, "Sta/Netmask", DEFAULT_STA_MK, NULL);
        wifi_sta_gateway = new IPaddrSetting("Station Static Gateway", WEBSET, WA, NULL, "Sta/Gateway", DEFAULT_STA_GW, NULL);
        wifi_sta_ip      = new IPaddrSetting("Station Static IP", WEBSET, WA, NULL, "Sta/IP", DEFAULT_STA_IP, NULL);
        wifi_sta_mode = new EnumSetting("Station IP Mode", WEBSET, WA, "ESP102", "Sta/IPMode", DEFAULT_STA_IP_MODE, &staModeOptions, NULL);
        wifi_sta_password = new StringSetting("Station Password",
                                              WEBSET,
                                              WA,
                                              "ESP101",
                                              "Sta/Password",
                                              DEFAULT_STA_PWD,
                                              MIN_PASSWORD_LENGTH,
                                              MAX_PASSWORD_LENGTH,
                                              (bool (*)(char*))WiFiConfig::isPasswordValid);
        wifi_sta_ssid     = new StringSetting("Station SSID",
                                          WEBSET,
                                          WA,
                                          "ESP100",
                                          "Sta/SSID",
                                          DEFAULT_STA_SSID,
                                          MIN_SSID_LENGTH,
                                          MAX_SSID_LENGTH,
                                          (bool (*)(char*))WiFiConfig::isSSIDValid);

        wifi_mode = new EnumSetting("WiFi mode", WEBSET, WA, "ESP116", "WiFi/Mode", WiFiFallback, &wifiModeOptions, NULL);

        new WebCommand(NULL, WEBCMD, WU, "ESP410", "WiFi/ListAPs", listAPs);
        new WebCommand(NULL, WEBCMD, WG, "ESP111", "System/IP", showIP);
        new WebCommand("IP=ipaddress MSK=netmask GW=gateway", WEBCMD, WA, "ESP103", "Sta/Setup", showSetStaParams);
#endif
    }

    void make_bt_settings() {
#ifdef ENABLE_BLUETOOTH

        bt_enable = new EnumSetting("Bluetooth Enable", WEBSET, WA, "ESP141", "Bluetooth/Enable", 1, &onoffOptions, NULL);

        bt_name = new StringSetting("Bluetooth name",
                                    WEBSET,
                                    WA,
                                    "ESP140",
                                    "Bluetooth/Name",
                                    DEFAULT_BT_NAME,
                                    WebUI::BTConfig::MIN_BTNAME_LENGTH,
                                    WebUI::BTConfig::MAX_BTNAME_LENGTH,
                                    (bool (*)(char*))BTConfig::isBTnameValid);
#endif
    }

    void make_authentication_settings() {
#ifdef ENABLE_AUTHENTICATION
        new WebCommand("password", WEBCMD, WA, "ESP555", "WebUI/SetUserPassword", setUserPassword);
        user_password  = new StringSetting("User password",
                                          WEBSET,
                                          WA,
                                          NULL,
                                          "WebUI/UserPassword",
                                          DEFAULT_USER_PWD,
                                          MIN_LOCAL_PASSWORD_LENGTH,
                                          MAX_LOCAL_PASSWORD_LENGTH,
                                          &COMMANDS::isLocalPasswordValid);
        admin_password = new StringSetting("Admin password",
                                           WEBSET,
                                           WA,
                                           NULL,
                                           "WebUI/AdminPassword",
                                           DEFAULT_ADMIN_PWD,
                                           MIN_LOCAL_PASSWORD_LENGTH,
                                           MAX_LOCAL_PASSWORD_LENGTH,
                                           &COMMANDS::isLocalPasswordValid);
#endif
    }

    void make_web_settings() {
        make_wifi_settings();
        make_bt_settings();
        make_authentication_settings();
        // If authentication enabled, display_settings skips or displays <Authentication Required>
        // RU - need user or admin password to read
        // WU - need user or admin password to set
        // WA - need admin password to set
        new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState);
        new WebCommand(NULL, WEBCMD, WU, "ESP420", "System/Stats", showSysStats, anyState);
        new WebCommand("RESTART", WEBCMD, WA, "ESP444", "System/Control", setSystemMode);
        new WebCommand("RESTART", WEBCMD, WA, NULL, "Bye", restart);

        new WebCommand(NULL, WEBCMD, WU, "ESP720", "LocalFS/Size", SPIFFSSize);
        new WebCommand("FORMAT", WEBCMD, WA, "ESP710", "LocalFS/Format", formatSpiffs);
        new WebCommand("path", WEBCMD, WU, "ESP701", "LocalFS/Show", showLocalFile);
        new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Delete", deleteLocalFile);

        new WebCommand("path", WEBCMD, WU, "ESP221", "SD/Show", showSDFile);
        new WebCommand("path", WEBCMD, WU, "ESP220", "SD/Run", runSDFile);
        new WebCommand("file_or_directory_path", WEBCMD, WU, "ESP215", "SD/Delete", deleteSDObject);
        new WebCommand(NULL, WEBCMD, WU, "ESP210", "SD/List", listSDFiles);
        new WebCommand(NULL, WEBCMD, WU, "ESP200", "SD/Status", showSDStatus);

        new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

        new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
        new WebCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);
    }
}
