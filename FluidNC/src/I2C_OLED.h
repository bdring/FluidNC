#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"

#include "Channel.h"
#include <SSD1306Wire.h>

class I2C_OLED : public Channel, public Configuration::Configurable {
private:
    std::string _report;

    String _radio_info;
    String _radio_addr;

    void parse_report();
    void parse_status_report();
    void parse_gcode_report();

    float* parse_axes(std::string s);
    void   parse_numbers(std::string s, float* nums, int maxnums);

    void setRadioString();

    void show_limits(bool probe, const bool* limits);
    void show_state(std::string& state);
    void show_file(float percent, const char* filename);
    void show_dro(const float* axes, bool is_mpos);
    void showRadioInfo();
    void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked);

    OLEDDISPLAY_GEOMETRY _geometry = GEOMETRY_64_48;

    bool _error = false;

public:
    I2C_OLED() : Channel("oled") {}

    I2C_OLED(const I2C_OLED&)            = delete;
    I2C_OLED(I2C_OLED&&)                 = delete;
    I2C_OLED& operator=(const I2C_OLED&) = delete;
    I2C_OLED& operator=(I2C_OLED&&)      = delete;

    virtual ~I2C_OLED() = default;

    void init();

    SSD1306Wire* _oled;

    // Configurable

    Pin     _sda_pin;
    Pin     _scl_pin;
    uint8_t _address = 0x3c;
    int     _width   = 64;
    int     _height  = 48;

    // Channel method overrides

    size_t write(uint8_t data) override;

    int read(void) override { return -1; }
    int peek(void) override { return -1; }

    Channel* pollLine(char* line) override;
    void     flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers:
    void validate() const override {
        Assert(!_sda_pin.undefined(), "I2C_OLED: sda_pin is undefined");
        Assert(!_scl_pin.undefined(), "I2C_OLED: scl_pin is undefined");
    }

    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        handler.item("sda_pin", _sda_pin);
        handler.item("scl_pin", _scl_pin);
        handler.item("i2c_address", _address);
        handler.item("width", _width);
        handler.item("height", _height);
    }
};
