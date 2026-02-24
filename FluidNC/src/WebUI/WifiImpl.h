#pragma once

#include "Settings.h"

#include <cstdint>
#include <string>

class JSONencoder;
class Channel;

namespace WebUI {
    const enum_opt_t* getWifiCountryOptions();
    int               getWifiCountryDefault();

    class WifiImpl {
    public:
        virtual ~WifiImpl() = default;

        virtual const enum_opt_t* staSecurityOptions() const = 0;
        virtual int               staSecurityDefault() const = 0;

        virtual bool              supportsPsMode() const          = 0;
        virtual const enum_opt_t* psModeOptions() const           = 0;
        virtual int               psModeDefault() const           = 0;

        virtual bool              allowRssiRead() const = 0;

        virtual void addWifiStatsPrefix(JSONencoder& j) const = 0;
        virtual void addStaPhyModeJson(JSONencoder& j) const = 0;
        virtual void addApDetailsJson(JSONencoder& j) const   = 0;

        virtual void addStatusPrefix(Channel& out) const       = 0;
        virtual void addStaPhyModeStatus(Channel& out) const = 0;
        virtual void addApDetailsStatus(Channel& out) const   = 0;

        virtual void prepareStartSta(int staMinSecurity, bool fastScan, const char* apCountry) = 0;
        virtual bool getStaBssidForSecurity(const char* ssid, int staMinSecurity, uint8_t outBssid[6], bool& useBssid) = 0;
        virtual bool beginSta(const char* ssid, const char* password, const uint8_t* bssid) = 0;

        virtual void prepareStartAp(const char* apCountry) = 0;

        virtual void onStaFallbackFailure() = 0;

        virtual void onWifiOn(int wifiPsMode) = 0;
        virtual void onWifiOff()                = 0;

        virtual std::string apInfoString() const = 0;

        virtual int32_t beginApListScan() = 0;
        virtual bool    isApProtected(int index) const = 0;
        virtual void    finishApListScan() = 0;

        virtual void poll() = 0;
    };

    WifiImpl& wifiImpl();
}
