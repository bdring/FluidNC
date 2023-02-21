#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"

#include "Channel.h"
#include "SSD1306_I2C.h"

typedef const uint8_t* font_t;

class OLED : public Channel, public Configuration::Configurable {
private:
    std::string _report;

    String _radio_info;
    String _radio_addr;

    std::string _state;

    int _radio_delay = 0;

    uint8_t _i2c_num = 0;

    void parse_report();
    void parse_status_report();
    void parse_gcode_report();
    void parse_STA();
    void parse_IP();
    void parse_AP();
    void parse_BT();

    float* parse_axes(std::string s);
    void   parse_numbers(std::string s, float* nums, int maxnums);

    void show_limits(bool probe, const bool* limits);
    void show_state(std::string& state);
    void show_file(float percent, const char* filename);
    void show_dro(const float* axes, bool is_mpos, bool* limits);
    void showRadioInfo();
    void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked);
    void wrappedDrawString(int16_t y, String& s, font_t font);

    uint8_t font_width(font_t font);
    uint8_t font_height(font_t font);
    size_t  charWidth(char s, font_t font);

    OLEDDISPLAY_GEOMETRY _geometry = GEOMETRY_64_48;

    bool _error = false;

public:
    OLED() : Channel("oled") {}

    OLED(const OLED&) = delete;
    OLED(OLED&&)      = delete;
    OLED& operator=(const OLED&) = delete;
    OLED& operator=(OLED&&) = delete;

    virtual ~OLED() = default;

    void init();

    OLEDDisplay* _oled;

    // Configurable

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
    void validate() const override {}

    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        handler.item("i2c_num", _i2c_num);
        handler.item("i2c_address", _address);
        handler.item("width", _width);
        handler.item("height", _height);
        handler.item("radio_delay_ms", _radio_delay);
    }
};
