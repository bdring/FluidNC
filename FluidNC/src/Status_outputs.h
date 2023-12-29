#pragma once

#include "Config.h"
#include "Configuration/Configurable.h"
#include "Channel.h"

typedef const uint8_t* font_t;

class Status_Outputs : public Channel, public Configuration::Configurable {
    Pin _Idle_pin;
    Pin _Run_pin;
    Pin _Hold_pin;
    Pin _Alarm_pin;

public:
private:
    std::string _report;
    std::string _state;

    int _report_interval_ms = 500;

    void parse_report();
    void parse_status_report();

public:
    Status_Outputs() : Channel("status_outputs") {}

    Status_Outputs(const Status_Outputs&) = delete;
    Status_Outputs(Status_Outputs&&)      = delete;
    Status_Outputs& operator=(const Status_Outputs&) = delete;
    Status_Outputs& operator=(Status_Outputs&&) = delete;

    virtual ~Status_Outputs() = default;

    void init();

    size_t write(uint8_t data) override;

    Channel* pollLine(char* line) override;
    void     flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers:
    void validate() override {}
    void afterParse() override {};

    void group(Configuration::HandlerBase& handler) override {
        handler.item("report_interval_ms", _report_interval_ms, 100, 5000);
        handler.item("idle_pin", _Idle_pin);
        handler.item("run_pin", _Run_pin);
        handler.item("hold_pin", _Hold_pin);
        handler.item("alarm_pin", _Alarm_pin);
    }
};
