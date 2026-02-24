#include "WifiImpl.h"

namespace WebUI {
    enum WiFiCountry {
        WiFiCountry01 = 0,
        WiFiCountryAT,
        WiFiCountryAU,
        WiFiCountryBE,
        WiFiCountryBG,
        WiFiCountryBR,
        WiFiCountryCA,
        WiFiCountryCH,
        WiFiCountryCN,
        WiFiCountryCY,
        WiFiCountryCZ,
        WiFiCountryDE,
        WiFiCountryDK,
        WiFiCountryEE,
        WiFiCountryES,
        WiFiCountryFI,
        WiFiCountryFR,
        WiFiCountryGB,
        WiFiCountryGR,
        WiFiCountryHK,
        WiFiCountryHR,
        WiFiCountryHU,
        WiFiCountryIE,
        WiFiCountryIN,
        WiFiCountryIS,
        WiFiCountryIT,
        WiFiCountryJP,
        WiFiCountryKR,
        WiFiCountryLI,
        WiFiCountryLT,
        WiFiCountryLU,
        WiFiCountryLV,
        WiFiCountryMT,
        WiFiCountryMX,
        WiFiCountryNL,
        WiFiCountryNO,
        WiFiCountryNZ,
        WiFiCountryPL,
        WiFiCountryPT,
        WiFiCountryRO,
        WiFiCountrySE,
        WiFiCountrySI,
        WiFiCountrySK,
        WiFiCountryTW,
        WiFiCountryUS,
    };

    static const enum_opt_t kWifiCountryOptionsMap = {
        { "01", WiFiCountry01 }, { "AT", WiFiCountryAT }, { "AU", WiFiCountryAU }, { "BE", WiFiCountryBE }, { "BG", WiFiCountryBG },
        { "BR", WiFiCountryBR }, { "CA", WiFiCountryCA }, { "CH", WiFiCountryCH }, { "CN", WiFiCountryCN }, { "CY", WiFiCountryCY },
        { "CZ", WiFiCountryCZ }, { "DE", WiFiCountryDE }, { "DK", WiFiCountryDK }, { "EE", WiFiCountryEE }, { "ES", WiFiCountryES },
        { "FI", WiFiCountryFI }, { "FR", WiFiCountryFR }, { "GB", WiFiCountryGB }, { "GR", WiFiCountryGR }, { "HK", WiFiCountryHK },
        { "HR", WiFiCountryHR }, { "HU", WiFiCountryHU }, { "IE", WiFiCountryIE }, { "IN", WiFiCountryIN }, { "IS", WiFiCountryIS },
        { "IT", WiFiCountryIT }, { "JP", WiFiCountryJP }, { "KR", WiFiCountryKR }, { "LI", WiFiCountryLI }, { "LT", WiFiCountryLT },
        { "LU", WiFiCountryLU }, { "LV", WiFiCountryLV }, { "MT", WiFiCountryMT }, { "MX", WiFiCountryMX }, { "NL", WiFiCountryNL },
        { "NO", WiFiCountryNO }, { "NZ", WiFiCountryNZ }, { "PL", WiFiCountryPL }, { "PT", WiFiCountryPT }, { "RO", WiFiCountryRO },
        { "SE", WiFiCountrySE }, { "SI", WiFiCountrySI }, { "SK", WiFiCountrySK }, { "TW", WiFiCountryTW }, { "US", WiFiCountryUS },
    };

    const enum_opt_t* getWifiCountryOptions() { return &kWifiCountryOptionsMap; }

    int getWifiCountryDefault() { return WiFiCountry01; }
}
