// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// CYW43 WiFi Diagnostics for RP2350/RP2040
// Provides low-level inspection of WiFi connectivity issues

#pragma once

#ifdef PICO_RP2350
#include <Arduino.h>

namespace CYW43_Diag {
    // Get detailed status information
    struct WiFiDiagnostics {
        uint8_t         wifi_status;           // WiFi.status() result
        bool            is_connected;          // WiFi.connected() result
        int32_t         rssi;                  // Signal strength (dBm)
        ip_addr_t       local_ip;              // Local IP address
        uint8_t         mac[6];                // MAC address
        const char*     ssid;                  // Connected SSID
        unsigned long   uptime_ms;             // Time since connection
        unsigned long   last_ping_response_ms; // Time of last ping response
    };

    inline WiFiDiagnostics get_diagnostics() {
        WiFiDiagnostics diag = {};
        diag.wifi_status        = WiFi.status();
        diag.is_connected       = WiFi.connected();
        diag.rssi               = WiFi.RSSI();
        diag.local_ip.addr      = WiFi.localIP();
        WiFi.macAddress(diag.mac);
        diag.ssid               = WiFi.SSID().c_str();
        diag.uptime_ms          = millis();
        return diag;
    }

    // Log detailed diagnostics
    inline void log_diagnostics() {
        auto diag = get_diagnostics();
        log_debug("=== WiFi Diagnostics ===");
        log_debug("Status: " << (int)diag.wifi_status);
        log_debug("Connected: " << (diag.is_connected ? "YES" : "NO"));
        log_debug("RSSI: " << diag.rssi << " dBm");
        log_debug("SSID: " << (diag.ssid ? diag.ssid : "N/A"));
        log_debug("IP: " << IP_string(diag.local_ip));
        log_debug("Uptime: " << diag.uptime_ms << " ms");
    }

    // WiFi status code meanings (from WiFi.h)
    inline const char* status_string(uint8_t status) {
        switch (status) {
            case WL_IDLE_STATUS:      return "IDLE";
            case WL_CONNECTING:       return "CONNECTING";
            case WL_CONNECTED:        return "CONNECTED";
            case WL_CONNECT_FAILED:   return "CONNECT_FAILED";
            case WL_DISCONNECTED:     return "DISCONNECTED";
            case WL_NO_SSID_AVAIL:    return "NO_SSID";
            case WL_SCAN_COMPLETED:   return "SCAN_COMPLETED";
            default:                  return "UNKNOWN";
        }
    }

    // Check for common issues
    inline void check_common_issues() {
        auto diag = get_diagnostics();
        
        log_debug("=== WiFi Health Check ===");
        
        // Check 1: Connected?
        if (!diag.is_connected) {
            log_warn("WiFi not connected! Status: " << status_string(diag.wifi_status));
        }
        
        // Check 2: Signal strength
        if (diag.is_connected && diag.rssi < -80) {
            log_warn("Weak WiFi signal: " << diag.rssi << " dBm");
        }
        
        // Check 3: Uptime - has connection survived long enough?
        if (diag.uptime_ms < 30000 && diag.is_connected) {
            log_debug("Connection is very new (" << diag.uptime_ms << " ms), verify stability");
        }
    }
}
#endif
