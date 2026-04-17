#pragma once

#include "WString.h"

#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

inline bool                     g_wifiClientConnectResult = true;
inline bool                     g_wifiClientConnected = false;
inline int                      g_wifiClientStopCalls = 0;
inline int                      g_wifiClientSetInsecureCalls = 0;
inline std::vector<std::string> g_wifiClientWrites;
inline std::deque<std::string>  g_wifiClientReadLines;
inline int                      g_wifiClientLastErrorCode = 0;
inline std::string              g_wifiClientLastErrorText;

class WiFiClientSecure {
public:
    bool connect(const char*, uint16_t) {
        g_wifiClientConnected = g_wifiClientConnectResult;
        return g_wifiClientConnectResult;
    }

    bool connected() {
        return g_wifiClientConnected;
    }

    void setInsecure() {
        ++g_wifiClientSetInsecureCalls;
    }

    void stop() {
        ++g_wifiClientStopCalls;
        g_wifiClientConnected = false;
    }

    size_t print(const char* text) {
        g_wifiClientWrites.emplace_back(text ? text : "");
        return text ? std::strlen(text) : 0;
    }

    size_t println(const char* text) {
        std::string line = text ? text : "";
        line += "\r\n";
        g_wifiClientWrites.emplace_back(line);
        return line.size();
    }

    size_t printf(const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (written < 0) {
            return 0;
        }
        g_wifiClientWrites.emplace_back(buffer);
        return static_cast<size_t>(written);
    }

    String readStringUntil(char) {
        if (g_wifiClientReadLines.empty()) {
            g_wifiClientConnected = false;
            return String("");
        }
        std::string line = g_wifiClientReadLines.front();
        g_wifiClientReadLines.pop_front();
        if (g_wifiClientReadLines.empty()) {
            g_wifiClientConnected = false;
        }
        return String(line.c_str());
    }

    int lastError(char* buffer, size_t len) {
        if (buffer && len) {
            std::snprintf(buffer, len, "%s", g_wifiClientLastErrorText.c_str());
        }
        return g_wifiClientLastErrorCode;
    }
};
