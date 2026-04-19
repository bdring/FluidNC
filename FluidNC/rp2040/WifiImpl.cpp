#ifdef PICO_RP2350
extern "C" {
#include <pico/cyw43_arch.h>
extern cyw43_t cyw43_state;
}
#endif
#include "../src/WebUI/WifiImpl.h"

#if defined(PICO_RP2040) || defined(PICO_RP2350)

#include "../src/Settings.h"
#include "../src/JSONEncoder.h"
#include "../src/Channel.h"
#include "../src/Logging.h"

#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <pico/cyw43_arch.h>

namespace WebUI {
    enum PicoStaMinSecurity {
        PicoStaMinSecurityOpen = 0,
        PicoStaMinSecurityWPA,
        PicoStaMinSecurityWPA2,
    };

    static const enum_opt_t picoStaSecurityOptions = {
        { "OPEN", PicoStaMinSecurityOpen },
        { "WPA-PSK", PicoStaMinSecurityWPA },
        { "WPA2-PSK", PicoStaMinSecurityWPA2 },
    };

    class Rp2xxxWifiImpl : public WifiImpl {
    private:
        static uint32_t cyw43CountryCode(const char* country) {
            if (country == nullptr || country[0] == '\0' || strcmp(country, "01") == 0) {
                return CYW43_COUNTRY_WORLDWIDE;
            }
            if (strlen(country) == 2) {
                return CYW43_COUNTRY(country[0], country[1], 0);
            }
            return CYW43_COUNTRY_WORLDWIDE;
        }

        static void applyCyw43Country(int interfaceId, const char* country) {
            uint32_t code = cyw43CountryCode(country);
            cyw43_wifi_set_up(&cyw43_state, interfaceId, true, code);
            if (country) {
                log_info("Set CYW43 regulatory domain to " << country);
            }
        }

        static int requiredSecurityLevel(int staMinSecurity) {
            switch (staMinSecurity) {
                case PicoStaMinSecurityWPA2:
                    return 2;
                case PicoStaMinSecurityWPA:
                    return 1;
                default:
                    return 0;
            }
        }

        static int securityLevel(uint8_t enc) {
            switch (enc) {
                case ENC_TYPE_NONE:
                    return 0;
                case ENC_TYPE_TKIP:
                    return 1;
                case ENC_TYPE_CCMP:
                    return 2;
                case ENC_TYPE_AUTO:
                    return 2;
                default:
                    return -1;
            }
        }

    public:
        const enum_opt_t* staSecurityOptions() const override { return &picoStaSecurityOptions; }
        int               staSecurityDefault() const override { return PicoStaMinSecurityWPA2; }

        bool              supportsPsMode() const override { return false; }
        const enum_opt_t* psModeOptions() const override { return nullptr; }
        int               psModeDefault() const override { return 0; }

        bool              allowRssiRead() const override { return false; }

        void addWifiStatsPrefix(JSONencoder& j) const override { (void)j; }
        void addStaPhyModeJson(JSONencoder& j) const override { (void)j; }
        void addApDetailsJson(JSONencoder& j) const override { (void)j; }

        void addStatusPrefix(Channel& out) const override { (void)out; }
        void addStaPhyModeStatus(Channel& out) const override { (void)out; }
        void addApDetailsStatus(Channel& out) const override { (void)out; }

        void prepareStartSta(int staMinSecurity, bool fastScan, const char* apCountry) override {
            (void)staMinSecurity;
            (void)fastScan;
            applyCyw43Country(CYW43_ITF_STA, apCountry);
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
            // Enable CYW43 verbose tracing for debugging
            // cyw43_state.trace_flags = 0xFFFF; // Enable all trace flags (MAC, ETH, etc)
            // Ensure STA interface is set up before setting power management
            // ::printf("set up\n");
            // cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true, CYW43_COUNTRY_WORLDWIDE);
            cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
            return WiFi.begin(ssid, password);
        }

        void prepareStartAp(const char* apCountry) override { applyCyw43Country(CYW43_ITF_AP, apCountry); }

        void onStaFallbackFailure() override {}

        void onWifiOn(int wifiPsMode) override { (void)wifiPsMode; }
        void onWifiOff() override {}

        std::string webAddressIp() const override {
            auto ip = WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP();
            return IP_string(ip);
        }

        std::string apInfoString() const override {
            std::string result;
            result += "Mode=AP:SSID=";
            result += WiFi.softAPSSID().c_str();
            result += ":IP=";
            result += IP_string(WiFi.softAPIP());
            result += ":MAC=";
            std::string mac(WiFi.softAPmacAddress().c_str());
            for (auto& ch : mac) {
                if (ch == ':') {
                    ch = '-';
                }
            }
            result += mac;
            return result;
        }

        int32_t beginApListScan() override {
            int32_t n = WiFi.scanNetworks(false);
            return n < 0 ? 0 : n;
        }

        bool isApProtected(int index) const override { return WiFi.encryptionType(index) != ENC_TYPE_NONE; }

        void finishApListScan() override { WiFi.scanDelete(); }

        void initNTP() override {
            NTP.begin("pool.ntp.org", "time.nist.gov");
        }

        void poll() override {
#ifdef PICO_RP2350
            delay(1);
#endif
        }
    };

    WifiImpl& wifiImpl() {
        static Rp2xxxWifiImpl instance;
        return instance;
    }
}

#endif
