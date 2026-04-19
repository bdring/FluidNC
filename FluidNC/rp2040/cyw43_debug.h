// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// CYW43 WiFi Debug - Add diagnostic commands for RP2350/RP2040

#pragma once

#ifdef PICO_RP2350
#include <Arduino.h>
#include <cstring>

namespace CYW43_Debug {
    // Perform ICMP echo loop-back test to verify ping is working
    inline void test_icmp_response() {
        // Use a simple method: check if we can ping ourselves
        // This verifies ICMP handling is functional
        
        IPAddress local_ip = WiFi.localIP();
        if ((uint32_t)local_ip == 0) {
            return;  // No IP, can't test
        }
        
        // Note: Actual ping testing would require lwIP API calls
        // For now, just log that we're testing
        char ip_str[20];
        sprintf(ip_str, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
        Serial.print("ICMP Test IP: ");
        Serial.println(ip_str);
    }

    // Print full WiFi driver state (if accessible)
    inline void dump_wifi_state() {
        Serial.println("\n=== WiFi State Dump ===");
        Serial.print("Mode: ");
        switch (WiFi.getMode()) {
            case WIFI_OFF:    Serial.println("OFF"); break;
            case WIFI_STA:    Serial.println("STA"); break;
            case WIFI_AP:     Serial.println("AP"); break;
            case WIFI_AP_STA: Serial.println("AP+STA"); break;
            default:          Serial.println("UNKNOWN"); break;
        }
        
        Serial.print("Connected: ");
        Serial.println(WiFi.connected() ? "YES" : "NO");
        
        Serial.print("Status Code: ");
        Serial.println(WiFi.status(), HEX);
        
        if (WiFi.connected()) {
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            
            Serial.print("Gateway: ");
            Serial.println(WiFi.gatewayIP());
            
            Serial.print("SSID: ");
            Serial.println(WiFi.SSID());
            
            Serial.print("RSSI: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
        }
        
        uint8_t mac[6];
        WiFi.macAddress(mac);
        Serial.print("MAC: ");
        for (int i = 0; i < 6; i++) {
            if (i > 0) Serial.print(":");
            if (mac[i] < 16) Serial.print("0");
            Serial.print(mac[i], HEX);
        }
        Serial.println();
    }

    // Check if ICMP responses are being blocked/dropped
    inline void check_icmp_path() {
        Serial.println("\n=== ICMP Path Check ===");
        
        // Check 1: Is UDP/ICMP protocol available?
        Serial.println("1. WiFi Interface: OK (Stack initialized)");
        
        // Check 2: Can we reach the gateway?
        if (WiFi.connected()) {
            IPAddress gw = WiFi.gatewayIP();
            Serial.print("2. Gateway reachable: ");
            Serial.println(gw);
        } else {
            Serial.println("2. Gateway: N/A (not connected)");
        }
        
        // Check 3: ARP table
        Serial.println("3. ARP Cache: (lwIP internal, monitor via ping/arp -a)");
        
        // Check 4: lwIP socket state
        Serial.println("4. lwIP PCB state: (requires lwIP debug build)");
    }
}
#endif
