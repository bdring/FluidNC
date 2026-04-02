#pragma once

#include <functional>

typedef int ota_error_t;

static constexpr ota_error_t OTA_AUTH_ERROR = 1;
static constexpr ota_error_t OTA_BEGIN_ERROR = 2;
static constexpr ota_error_t OTA_CONNECT_ERROR = 3;
static constexpr ota_error_t OTA_RECEIVE_ERROR = 4;
static constexpr ota_error_t OTA_END_ERROR = 5;
static constexpr int U_FLASH = 0;
static constexpr int U_FS = 100;

class ArduinoOTAClass {
public:
    bool mdnsEnabled = true;
    const char* hostname = nullptr;
    int command = U_FLASH;
    int beginCalls = 0;
    int endCalls = 0;
    int handleCalls = 0;
    std::function<void()> onStartHandler;
    std::function<void()> onEndHandler;
    std::function<void(unsigned int, unsigned int)> onProgressHandler;
    std::function<void(ota_error_t)> onErrorHandler;

    ArduinoOTAClass& setMdnsEnabled(bool enabled) {
        mdnsEnabled = enabled;
        return *this;
    }
    ArduinoOTAClass& setHostname(const char* next) {
        hostname = next;
        return *this;
    }
    ArduinoOTAClass& onStart(std::function<void()> handler) {
        onStartHandler = std::move(handler);
        return *this;
    }
    ArduinoOTAClass& onEnd(std::function<void()> handler) {
        onEndHandler = std::move(handler);
        return *this;
    }
    ArduinoOTAClass& onProgress(std::function<void(unsigned int, unsigned int)> handler) {
        onProgressHandler = std::move(handler);
        return *this;
    }
    ArduinoOTAClass& onError(std::function<void(ota_error_t)> handler) {
        onErrorHandler = std::move(handler);
        return *this;
    }
    void begin() {
        ++beginCalls;
    }
    void end() {
        ++endCalls;
    }
    void handle() {
        ++handleCalls;
    }
    int getCommand() const {
        return command;
    }
};

inline ArduinoOTAClass ArduinoOTA;
