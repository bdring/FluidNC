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

        virtual bool              supportsPsMode() const = 0;
        virtual const enum_opt_t* psModeOptions() const  = 0;
        virtual int               psModeDefault() const  = 0;

        virtual bool allowRssiRead() const = 0;

        virtual void addWifiStatsPrefix(JSONencoder& j) const = 0;
        virtual void addStaPhyModeJson(JSONencoder& j) const  = 0;
        virtual void addApDetailsJson(JSONencoder& j) const   = 0;

        virtual void addStatusPrefix(Channel& out) const     = 0;
        virtual void addStaPhyModeStatus(Channel& out) const = 0;
        virtual void addApDetailsStatus(Channel& out) const  = 0;

        virtual void prepareStartSta(int staMinSecurity, bool fastScan, const char* apCountry)                         = 0;
        virtual bool getStaBssidForSecurity(const char* ssid, int staMinSecurity, uint8_t outBssid[6], bool& useBssid) = 0;
        virtual bool beginSta(const char* ssid, const char* password, const uint8_t* bssid)                            = 0;

        virtual bool setStaticIP(uint32_t ip, uint32_t dns, uint32_t gateway, uint32_t netmask) = 0;

        virtual void prepareStartAp(const char* apCountry) = 0;

        virtual void onStaFallbackFailure() = 0;

        virtual void onWifiOn(int wifiPsMode) = 0;
        virtual void onWifiOff()              = 0;

        virtual std::string webAddressIp() const = 0;
        virtual std::string apInfoString() const = 0;

        // AP-list scan.  The primitives are non-blocking:
        //   startApListScan()  - launch a scan unless one is already running
        //                        or its results are still available
        //   apListScanState()  - Running: in progress; Done: results ready
        //                        (apListCount() valid, may be 0); Failed: no
        //                        scan running or complete, (re)start one
        //   apListCount() / isApProtected(i) - read completed results
        //   finishApListScan() - release the results
        // beginApListScan() is the blocking convenience used by the
        // non-async ESP410 path; it is defined once in WifiImplCommon.cpp in
        // terms of the primitives above.
        enum class ApScanState { Running, Done, Failed };
        virtual void        startApListScan()          = 0;
        virtual ApScanState apListScanState()          = 0;
        virtual int32_t     apListCount()              = 0;
        virtual bool        isApProtected(int index) const = 0;
        virtual void        finishApListScan()         = 0;

        int32_t beginApListScan();

        virtual void initNTP() = 0;

        virtual void poll() = 0;
    };

    WifiImpl& wifiImpl();

    // Encodes the ESP410 / [WiFi/ListAPs] response body (the AP list) from a
    // completed scan into `j`.  Shared by the synchronous command handler
    // (WifiConfig.cpp) and the async one (WifiScanAsync.cpp) so both return
    // the same shape.  `apCount` comes from beginApListScan()/apListCount();
    // `jsonWrapper` selects the {"cmd":"410",...,"data":[...]} envelope over
    // the bare {"AP_LIST":[...]}.
    void encodeApList(JSONencoder& j, int32_t apCount, bool jsonWrapper);
}
