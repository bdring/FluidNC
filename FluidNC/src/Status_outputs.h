#pragma once

#include "Config.h"
#include "Module.h"
#include "Channel.h"

class Status_Outputs : public Channel, public ConfigurableModule {
    Pin _Idle_pin;
    Pin _Run_pin;
    Pin _Hold_pin;
    Pin _Alarm_pin;
    Pin _Door_pin;

public:
private:
    std::string _report;
    std::string _state;

    int32_t _report_interval_ms = 500;

    void parse_report();
    void parse_status_report();

public:
    Status_Outputs(const char* name) : Channel(name), ConfigurableModule(name) {}

    Status_Outputs(const Status_Outputs&)            = delete;
    Status_Outputs(Status_Outputs&&)                 = delete;
    Status_Outputs& operator=(const Status_Outputs&) = delete;
    Status_Outputs& operator=(Status_Outputs&&)      = delete;

    virtual ~Status_Outputs() = default;

    void init() override;

    size_t write(uint8_t data) override;

    Error pollLine(char* line) override;
    void  flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers:
    void validate() override {}
    void afterParse() override {};

    void group(Configuration::HandlerBase& handler) override {
        // Ties output pins to machine status states, e.g. to drive a stack light. All 5
        // pins below are outputs the firmware drives based on current state (not inputs,
        // despite how some documentation has described them) -- invert with a pin's :low
        // attribute if the connected hardware needs the opposite polarity.

        // @config report_interval_ms
        // @default 500
        // How often, in milliseconds, the output pins are refreshed against current status.
        // Doesn't need to be fast -- an update also happens immediately on every status
        // change regardless of this interval.
        handler.item("report_interval_ms", _report_interval_ms, 100, 5000);

        // @config idle_pin
        // @default NO_PIN
        // Active while machine status is Idle.
        handler.item("idle_pin", _Idle_pin);

        // @config run_pin
        // @default NO_PIN
        // Active while machine status is Run.
        handler.item("run_pin", _Run_pin);

        // @config hold_pin
        // @default NO_PIN
        // Active while machine status is Hold.
        handler.item("hold_pin", _Hold_pin);

        // @config alarm_pin
        // @default NO_PIN
        // Active while machine status is Alarm.
        handler.item("alarm_pin", _Alarm_pin);

        // @config door_pin
        // @default NO_PIN
        // Active while the safety door input is active.
        handler.item("door_pin", _Door_pin);
    }
};
