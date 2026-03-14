#pragma once

#include "esp_wifi.h"

class WiFiClass {
public:
    wifi_mode_t mode = WIFI_OFF;
    const char* hostname = "fluidnc";

    wifi_mode_t getMode() const {
        return mode;
    }

    const char* getHostname() const {
        return hostname;
    }

    void setMode(wifi_mode_t next) {
        mode = next;
    }

    void setHostname(const char* next) {
        hostname = next;
    }
};

inline WiFiClass WiFi;
