#include "../src/WebUI/WifiImpl.h"

#if defined(ESP32) && __has_include(<esp_wifi.h>) && __has_include(<esp_ota_ops.h>) && __has_include(<esp_idf_version.h>)

#include "../src/Settings.h"
#include "../src/JSONEncoder.h"
#include "../src/Channel.h"
#include "../src/Logging.h"
#include "Driver/Console.h"
#include "../src/Main.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_idf_version.h>
#include <cstring>

#if ESP_IDF_VERSION_MAJOR >= 5
#    include <esp_wifi.h>

#    define tcpip_adapter_sta_list_t wifi_sta_list_t
#    define tcpip_adapter_get_sta_list(station, list) esp_wifi_ap_get_sta_list(list)
#    define SYSTEM_EVENT_STA_GOT_IP IP_EVENT_STA_GOT_IP
#    define WIFI_EVENT_STA_DISCONNECTED WIFI_EVENT_STA_DISCONNECTED
#    define WIFI_EVENT_STA_START WIFI_EVENT_STA_START
#    define WIFI_EVENT_STA_STOP WIFI_EVENT_STA_STOP
#    define WIFI_EVENT_STA_CONNECTED WIFI_EVENT_STA_CONNECTED
#    define GetIPAddr(x) "0.0.0.0"
#else
#    define GetIPAddr(x) IP_string(IPAddress(x.ip.addr))
#endif

namespace WebUI {
    static const enum_opt_t esp32PsModeOptions = {
        { "None", WIFI_PS_NONE },
        { "Min", WIFI_PS_MIN_MODEM },
        { "Max", WIFI_PS_MAX_MODEM },
    };

    static const enum_opt_t esp32StaSecurityOptions = {
        { "OPEN", WIFI_AUTH_OPEN },
        { "WEP", WIFI_AUTH_WEP },
        { "WPA-PSK", WIFI_AUTH_WPA_PSK },
        { "WPA2-PSK", WIFI_AUTH_WPA2_PSK },
        { "WPA-WPA2-PSK", WIFI_AUTH_WPA_WPA2_PSK },
        { "WPA2-ENTERPRISE", WIFI_AUTH_WPA2_ENTERPRISE },
        { "WPA3-PSK", WIFI_AUTH_WPA3_PSK },
        { "WPA2-WPA3-PSK", WIFI_AUTH_WPA2_WPA3_PSK },
        { "WAPI-PSK", WIFI_AUTH_WAPI_PSK },
        { "WPA3-ENT-192", WIFI_AUTH_WPA3_ENT_192 },
    };

    static const char* mac2str(uint8_t mac[8]) {
        static char macstr[18];
        if (0 > snprintf(macstr, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5])) {
            strcpy(macstr, "00:00:00:00:00:00");
        }
        return macstr;
    }

    static void wifiEventHandler(WiFiEvent_t event) {
        static bool disconnect_seen = false;
        switch (event) {
            case SYSTEM_EVENT_STA_GOT_IP:
                log_info_to(Console, "Got IP: " << IP_string(WiFi.localIP()));
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (!disconnect_seen) {
                    log_info_to(Console, "WiFi Disconnected");
                    disconnect_seen = true;
                }
                break;
            case WIFI_EVENT_STA_START:
                break;
            case WIFI_EVENT_STA_STOP:
                break;
            case WIFI_EVENT_STA_CONNECTED:
                disconnect_seen = false;
                log_info_to(Console, "WiFi STA Connected");
                break;
            default:
                log_debug_to(Console, "WiFi event: " << (int)event);
                break;
        }
    }

    class Esp32WifiImpl : public WifiImpl {
    public:
        const enum_opt_t* staSecurityOptions() const override { return &esp32StaSecurityOptions; }
        int               staSecurityDefault() const override { return WIFI_AUTH_WPA2_PSK; }

        bool              supportsPsMode() const override { return true; }
        const enum_opt_t* psModeOptions() const override { return &esp32PsModeOptions; }
        int               psModeDefault() const override { return WIFI_PS_NONE; }

        bool              allowRssiRead() const override { return true; }

        void addWifiStatsPrefix(JSONencoder& j) const override {
            j.id_value_object("Sleep mode", WiFi.getSleep() ? "Modem" : "None");

            size_t flashsize = 0;
            if (esp_ota_get_running_partition()) {
                const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                if (partition) {
                    flashsize = partition->size;
                }
            }
            j.id_value_object("Available Size for update", formatBytes(flashsize));
        }

        void addStaPhyModeJson(JSONencoder& j) const override {
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

            j.id_value_object("Phy Mode", modeName);
        }

        void addApDetailsJson(JSONencoder& j) const override {
            wifi_config_t  conf;
            wifi_country_t country;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            esp_wifi_get_country(&country);
            j.id_value_object("SSID", (const char*)conf.ap.ssid);
            j.id_value_object("Visible", (conf.ap.ssid_hidden == 0 ? "Yes" : "No"));
            j.id_value_object("Radio country set",
                              std::string("") + country.cc[0] + country.cc[1] + " (channels " + std::to_string(country.schan) + "-" +
                                  std::to_string((country.schan + country.nchan - 1)) + ", max power " +
                                  std::to_string(country.max_tx_power) + "dBm)");
            const char* mode;
            switch (conf.ap.authmode) {
                case WIFI_AUTH_OPEN:
                    mode = "None";
                    break;
                case WIFI_AUTH_WEP:
                    mode = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    mode = "WPA-PSK";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    mode = "WPA2-PSK";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    mode = "WPA-WPA2-PSK";
                    break;
                default:
                    mode = "WPA/WPA2";
            }

            j.id_value_object("Authentication", mode);
            j.id_value_object("Max Connections", conf.ap.max_connection);

#if defined(IDFBUILD)
            wifi_sta_list_t          station;
            tcpip_adapter_sta_list_t tcpip_sta_list;
            esp_wifi_ap_get_sta_list(&station);
            tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
            j.id_value_object("Connected channels", station.num);

            for (int i = 0; i < station.num; i++) {
                j.id_value_object("", std::string("") + mac2str(tcpip_sta_list.sta[i].mac) + " " + GetIPAddr(tcpip_sta_list.sta[i]));
            }
#else
            wifi_sta_list_t      station;
            esp_netif_sta_list_t netif_sta_list;
            esp_wifi_ap_get_sta_list(&station);
            esp_netif_get_sta_list(&station, &netif_sta_list);

            j.id_value_object("Connected channels", station.num);

            for (size_t i = 0; i < station.num; i++) {
                j.id_value_object("",
                                  std::string("") + mac2str(netif_sta_list.sta[i].mac) + " " +
                                      IP_string(IPAddress(netif_sta_list.sta[i].ip.addr)));
            }
#endif
        }

        void addStatusPrefix(Channel& out) const override {
            log_stream(out, "Sleep mode: " << (WiFi.getSleep() ? "Modem" : "None"));
            size_t flashsize = 0;
            if (esp_ota_get_running_partition()) {
                const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                if (partition) {
                    flashsize = partition->size;
                }
            }
            log_stream(out, "Available Size for update: " << formatBytes(flashsize));
        }

        void addStaPhyModeStatus(Channel& out) const override {
            uint8_t PhyMode;
            esp_wifi_get_protocol(WIFI_IF_STA, &PhyMode);
            const char* phyModeName;
            switch (PhyMode) {
                case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N:
                    phyModeName = "11n";
                    break;
                case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G:
                    phyModeName = "11g";
                    break;
                case WIFI_PROTOCOL_11B:
                    phyModeName = "11b";
                    break;
                default:
                    phyModeName = "???";
            }
            log_stream(out, "Phy Mode: " << phyModeName);
        }

        void addApDetailsStatus(Channel& out) const override {
            wifi_config_t  conf;
            wifi_country_t country;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            esp_wifi_get_country(&country);
            log_stream(out, "SSID: " << (const char*)conf.ap.ssid);
            log_stream(out, "Visible: " << (conf.ap.ssid_hidden == 0 ? "Yes" : "No"));
            log_stream(out,
                       "Radio country set: " << country.cc[0] << country.cc[1] << " (channels " << country.schan << "-"
                                             << (country.schan + country.nchan - 1) << ", max power " << country.max_tx_power
                                             << "dBm)");

            const char* mode;
            switch (conf.ap.authmode) {
                case WIFI_AUTH_OPEN:
                    mode = "None";
                    break;
                case WIFI_AUTH_WEP:
                    mode = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    mode = "WPA-PSK";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    mode = "WPA2-PSK";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    mode = "WPA-WPA2-PSK";
                    break;
                default:
                    mode = "WPA/WPA2";
            }

            log_stream(out, "Authentication: " << mode);
            log_stream(out, "Max Connections: " << conf.ap.max_connection);

#if defined(IDFBUILD)
            wifi_sta_list_t          station;
            tcpip_adapter_sta_list_t tcpip_sta_list;
            esp_wifi_ap_get_sta_list(&station);
            tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
            log_stream(out, "Connected channels: " << station.num);

            for (int i = 0; i < station.num; i++) {
                log_stream(out, mac2str(tcpip_sta_list.sta[i].mac) << " " << GetIPAddr(tcpip_sta_list.sta[i]));
            }
#else
            wifi_sta_list_t      station;
            esp_netif_sta_list_t netif_sta_list;
            esp_wifi_ap_get_sta_list(&station);
            esp_netif_get_sta_list(&station, &netif_sta_list);
            log_stream(out, "Connected channels: " << station.num);

            for (size_t i = 0; i < station.num; i++) {
                log_stream(out, mac2str(netif_sta_list.sta[i].mac) << " " << IP_string(IPAddress(netif_sta_list.sta[i].ip.addr)));
            }
#endif
        }

        void prepareStartSta(int staMinSecurity, bool fastScan, const char* apCountry) override {
            (void)apCountry;
            WiFi.enableAP(false);
            WiFi.setMinSecurity(static_cast<wifi_auth_mode_t>(staMinSecurity));
            WiFi.setScanMethod(fastScan ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
            WiFi.setAutoReconnect(true);
        }

        bool getStaBssidForSecurity(const char* ssid, int staMinSecurity, uint8_t outBssid[6], bool& useBssid) override {
            (void)ssid;
            (void)staMinSecurity;
            (void)outBssid;
            useBssid = false;
            return true;
        }

        bool beginSta(const char* ssid, const char* password, const uint8_t* bssid) override {
            (void)bssid;
            return WiFi.begin(ssid, password);
        }

        void prepareStartAp(const char* apCountry) override {
            WiFi.enableSTA(false);
            if (apCountry && ESP_OK != esp_wifi_set_country_code(apCountry, true)) {
                log_error("failed to set Wifi regulatory domain to " << apCountry);
            }
        }

        void onStaFallbackFailure() override { esp_wifi_restore(); }

        void onWifiOn(int wifiPsMode) override {
            static bool eventsRegistered = false;
            if (!eventsRegistered) {
                WiFi.onEvent(wifiEventHandler);
                eventsRegistered = true;
            }
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_wifi_set_ps(static_cast<wifi_ps_type_t>(wifiPsMode));
        }

        void onWifiOff() override {
            WiFi.enableSTA(false);
            WiFi.enableAP(false);
        }

        std::string apInfoString() const override {
            std::string result;
            result += "Mode=AP:SSID=";
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            result += (const char*)conf.ap.ssid;
            result += ":IP=";
            result += IP_string(WiFi.softAPIP());
            result += ":MAC=";
            std::string mac(WiFi.softAPmacAddress().c_str());
            std::replace(mac.begin(), mac.end(), ':', '-');
            result += mac;
            return result;
        }

        int32_t beginApListScan() override {
            while (true) {
                int32_t n = WiFi.scanComplete();
                if (n >= 0) {
                    return n;
                }
                if (n == WIFI_SCAN_FAILED) {
                    WiFi.scanNetworks(true, false, false, 1000);
                }
                delay(1000);
            }
        }

        bool isApProtected(int index) const override { return WiFi.encryptionType(index) != WIFI_AUTH_OPEN; }

        void finishApListScan() override {
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
        }

        void poll() override {
            if (WiFi.getMode() == WIFI_AP_STA) {
                if (WiFi.scanComplete() >= 0) {
                    WiFi.enableSTA(false);
                }
            }
        }
    };

    WifiImpl& wifiImpl() {
        static Esp32WifiImpl instance;
        return instance;
    }
}

#endif
