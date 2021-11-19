#include "Arduino.h"

#include "SoftwareGPIO.h"
#include "Capture.h"

#include <chrono>
#include <thread>

int64_t esp_timer_get_time() {
    return Capture::instance().current();
}

void attachInterrupt(uint8_t pin, void (*callback)(void), int mode) {
    attachInterruptArg(
        pin,
        [](void* arg) {
            auto callback = reinterpret_cast<void (*)()>(arg);
            callback();
        },
        reinterpret_cast<void*>(callback),
        mode);
}

void attachInterruptArg(uint8_t pin, void (*callback)(void*), void* arg, int mode) {
    SoftwareGPIO::instance().attachISR(pin, callback, arg, mode);
}

void detachInterrupt(uint8_t pin) {
    SoftwareGPIO::instance().detachISR(pin);
}

extern "C" int __digitalRead(uint8_t pin) {
    auto& io = SoftwareGPIO::instance();
    return io.read(pin);
}

extern "C" void __pinMode(uint8_t pin, uint8_t mode) {
    auto& io = SoftwareGPIO::instance();
    io.setMode(pin, mode);
}

extern "C" void __digitalWrite(uint8_t pin, uint8_t val) {
    auto& io = SoftwareGPIO::instance();
    return io.writeOutput(pin, val ? true : false);
}

void delay(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int temperatureRead(void) {
    return 22;  // Nobody cares
}

esp_reset_reason_t esp_reset_reason(void) {
    return ESP_RST_POWERON;
}

uint64_t EspClass::getEfuseMac() {
    return 0x0102030405060708ULL;
}
uint32_t EspClass::getCpuFreqMHz() {
    return 240;
}
const char* EspClass::getSdkVersion() {
    return "v1.0-UnitTest-foobar";
}
uint32_t EspClass::getFreeHeap() {
    return 0xFFFF;
}
uint32_t EspClass::getFlashChipSize() {
    return 4 * 1024 * 1024;
}

void EspClass::restart() {
    throw SystemRestartException();
}

EspClass ESP;
