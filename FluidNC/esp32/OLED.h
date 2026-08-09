#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"

#include "Channel.h"
#include "Module.h"
#include "SSD1306_I2C.h"

typedef const uint8_t* font_t;

class OLED : public Channel, public ConfigurableModule {
public:
    OLED(const char* name) : Channel(name), ConfigurableModule(name) {}
    struct Layout {
        uint8_t                    _x;
        uint8_t                    _y;
        uint8_t                    _width_required;
        font_t                     _font;
        OLEDDISPLAY_TEXT_ALIGNMENT _align;
    };
    static Layout bannerLayout128;
    static Layout bannerLayout64;
    static Layout stateLayout;
    static Layout tickerLayout;
    static Layout filenameLayout;
    static Layout percentLayout128;
    static Layout percentLayout64;
    static Layout limitLabelLayout;
    static Layout posLabelLayout;
    static Layout radioAddrLayout;

private:
    std::string _report;

    std::string _radio_info;
    std::string _radio_addr;

    std::string _state;
    std::string _filename;

    float       _percent;
    std::string _ticker;

    int32_t _radio_delay        = 0;
    int32_t _report_interval_ms = 500;

    uint8_t _i2c_num = 0;

    void parse_report();
    void parse_status_report();
    void parse_gcode_report();
    void parse_STA();
    void parse_IP();
    void parse_AP();
    void parse_BT();
    void parse_WebUI();

    void parse_axes(std::string s, float* axes);
    void parse_numbers(std::string s, float* nums, uint8_t maxnums);

    void show_limits(bool probe, const bool* limits);
    void show_state();
    void show_file();
    void show_dro(const float* axes, bool isMpos, bool* limits);
    void show_radio_info();
    void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked);

    void wrapped_draw_string(int16_t y, const std::string& s, font_t font);

    void show(Layout& layout, const std::string& msg) { show(layout, msg.c_str()); }
    void show(Layout& layout, const char* msg);

    uint8_t font_width(font_t font);
    uint8_t font_height(font_t font);
    size_t  char_width(char s, font_t font);

    OLEDDISPLAY_GEOMETRY _geometry = GEOMETRY_64_48;

    bool _error = false;

public:
    OLED(const OLED&)            = delete;
    OLED(OLED&&)                 = delete;
    OLED& operator=(const OLED&) = delete;
    OLED& operator=(OLED&&)      = delete;

    virtual ~OLED() = default;

    void init() override;

    OLEDDisplay* _oled;

    // Configurable

    uint8_t _address = 0x3c;
    int32_t _width   = 64;
    int32_t _height  = 48;
    bool    _flip    = true;
    bool    _mirror  = false;

    // Channel method overrides
    size_t write(uint8_t data) override;

    int read(void) override { return -1; }
    int peek(void) override { return -1; }

    Error pollLine(char* line) override;
    void  flushRx() override {}

    bool   lineComplete(char*, char) override { return false; }
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override { return 0; }

    // Configuration handlers:
    void validate() override {}

    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        // @config report_interval_ms
        // @default 500
        // @tuning typical
        // Interval, in milliseconds, at which the display's status content (DRO, state,
        // filename/percent) is refreshed.
        handler.item("report_interval_ms", _report_interval_ms, 100, 5000);

        // @config i2c_num
        // @default 0
        // @tuning per-machine
        // Which top-level i2cN: bus this display is wired to.
        handler.item("i2c_num", _i2c_num);

        // @config i2c_address
        // @default 0x3c
        // @tuning per-machine
        // I2C address of the display module. Must match the actual hardware -- common
        // SSD1306 modules use 0x3C or 0x3D.
        handler.item("i2c_address", _address);

        // @config width
        // @default 64
        // @tuning per-machine
        // Physical panel width, in pixels. Must match the actual display module -- there's
        // no generic value that works across different panels.
        handler.item("width", _width);

        // @config height
        // @default 48
        // @tuning per-machine
        // Physical panel height, in pixels. Must match the actual display module -- there's
        // no generic value that works across different panels.
        handler.item("height", _height);

        // @config flip
        // @default true
        // @tuning per-machine
        // Rotates the displayed content 180 degrees. Depends on how the panel is physically
        // mounted.
        handler.item("flip", _flip);

        // @config mirror
        // @default false
        // @tuning per-machine
        // Mirrors the displayed content horizontally. Depends on how the panel is physically
        // mounted.
        handler.item("mirror", _mirror);

        // @config radio_delay_ms
        // @default 0
        // @tuning per-machine
        // Delay, in milliseconds, before the display shows radio (WiFi/BT) connection info
        // after boot -- gives the radio hardware time to come up first.
        handler.item("radio_delay_ms", _radio_delay);
    }
};
