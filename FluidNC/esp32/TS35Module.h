#pragma once

#include "Channel.h"
#include "Module.h"
#include "Pin.h"
#include "TS35DisplayDriver.h"
#include "TS35Model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ts35 {

    class TS35Module : public Channel, public ConfigurableModule {
    public:
        explicit TS35Module(const char* name) : Channel(name), ConfigurableModule(name) {}

        TS35Module(const TS35Module&)            = delete;
        TS35Module(TS35Module&&)                 = delete;
        TS35Module& operator=(const TS35Module&) = delete;
        TS35Module& operator=(TS35Module&&)      = delete;

        void init() override;
        void deinit() override;

        size_t write(uint8_t data) override;
        Error  pollLine(char* line) override;

        // The command is metadata only; it never starts, pauses, or modifies a job.

        void validate() override;
        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;

    private:
        enum class Page : uint8_t { Dro = 0, Jog, Zero, Control, Info };
        static constexpr int PageCount = 5;
        enum class AlarmClear : uint8_t { None = 0, ResetSent };
        enum class ConfirmAction : uint8_t { None = 0, ZeroX, ZeroY, ZeroZ, ZeroXYZ, Home, Unlock, SoftReset };

        static constexpr int16_t ScreenWidth  = 480;
        static constexpr int16_t ScreenHeight = 320;
        static constexpr int16_t NavTop       = 280;
        static constexpr int16_t NavHeight    = 40;
        static constexpr int16_t NavWidth     = ScreenWidth / PageCount;

        // One slice of a full-screen wipe, see clearNextBand().
        static constexpr int16_t ClearBandHeight = 32;

        // The strip between the page body and the tab bar belongs to the notice
        // line alone, so nothing else may be placed on those rows.
        static constexpr int16_t NoticeY = 268;

        // Jog page. The left and right columns are the axis buttons, the middle is
        // the readout, and the strip along the bottom holds the three actions.
        static constexpr int16_t JogBtnW        = 125;
        static constexpr int16_t JogBtnH        = 44;
        static constexpr int16_t JogLeftX       = 18;
        static constexpr int16_t JogRightX      = 337;
        static constexpr int16_t JogRowY[3]     = { 52, 104, 156 };
        static constexpr int16_t JogReadLetterX = 150;
        static constexpr int16_t JogReadValueX  = 172;
        static constexpr int16_t JogActionY     = 208;
        static constexpr int16_t JogActionH     = 46;
        static constexpr int16_t JogStepX       = 12;
        static constexpr int16_t JogStepW       = 148;
        static constexpr int16_t JogCancelX     = 166;
        static constexpr int16_t JogCancelW     = 148;
        static constexpr int16_t JogSpeedX      = 320;
        static constexpr int16_t JogSpeedW      = 142;
        static constexpr int16_t JogNoteY       = 256;

        // DRO page.
        static constexpr int16_t DroFooterY = 247;

        // Zero page: two columns of two buttons, plus the hint line under them.
        static constexpr int16_t ZeroBtnW  = 205;
        static constexpr int16_t ZeroBtnH  = 72;
        static constexpr int16_t ZeroX[2]  = { 20, 255 };
        static constexpr int16_t ZeroY[2]  = { 62, 156 };
        static constexpr int16_t ZeroHintY = 245;

        // Control page: three wide buttons, a row of override buttons, then three
        // more wide buttons.
        static constexpr int16_t CtrlTopY      = 58;
        static constexpr int16_t CtrlTopH      = 54;
        static constexpr int16_t CtrlOvrY      = 142;
        static constexpr int16_t CtrlOvrH      = 45;
        static constexpr int16_t CtrlBotY      = 211;
        static constexpr int16_t CtrlBotH      = 43;
        static constexpr int16_t CtrlWideW     = 135;
        static constexpr int16_t CtrlWideX[3]  = { 18, 172, 326 };
        static constexpr int16_t CtrlOvrW      = 100;
        static constexpr int16_t CtrlOvrX[3]   = { 18, 135, 252 };
        static constexpr int16_t CtrlRapidX    = 369;
        static constexpr int16_t CtrlRapidW    = 92;
        static constexpr int16_t CtrlOverrideY = 124;

        // Alarm banner: the band just above the tab bar, drawn on any page.
        static constexpr int16_t AlarmTop     = 192;
        static constexpr int16_t AlarmHeight  = 84;
        static constexpr int16_t AlarmButtonX = 300;
        static constexpr int16_t AlarmButtonY = 218;
        static constexpr int16_t AlarmButtonW = 170;
        static constexpr int16_t AlarmButtonH = 48;

        static constexpr uint16_t Black     = 0x0000;
        static constexpr uint16_t White     = 0xffff;
        static constexpr uint16_t Red       = 0xf800;
        static constexpr uint16_t Green     = 0x07e0;
        static constexpr uint16_t DarkGreen = 0x03e0;
        static constexpr uint16_t Blue      = 0x001f;
        static constexpr uint16_t Yellow    = 0xffe0;
        static constexpr uint16_t Cyan      = 0x07ff;
        static constexpr uint16_t DarkGray  = 0x4208;
        static constexpr uint16_t LightGray = 0x9cd3;
        static constexpr uint16_t Navy      = 0x000f;
        static constexpr uint16_t Orange    = 0xfd20;

        // Configuration.
        int32_t _report_interval_ms = 200;
        int32_t _render_interval_ms = 200;
        int32_t _touch_interval_ms  = 40;

        bool _controls_enabled = false;
        bool _allow_jog        = true;
        bool _allow_zero       = true;
        bool _allow_homing     = false;
        bool _allow_unlock     = false;
        bool _allow_soft_reset = false;
        // Independent of _controls_enabled: neither the soft reset nor the $X moves
        // the machine.
        bool _allow_alarm_clear = false;

        float   _jog_step_mm        = 0.1f;
        float   _jog_feed_xy_mm_min = 1000.0f;
        float   _jog_feed_z_mm_min  = 500.0f;
        int32_t _jog_percent        = 80;
        float   _jog_step_max_mm    = 10.0f;
        float   _jog_step_max_z_mm  = 10.0f;

        int32_t _touch_x_min              = 300;
        int32_t _touch_x_max              = 3600;
        int32_t _touch_y_min              = 300;
        int32_t _touch_y_max              = 3600;
        int32_t _touch_pressure_threshold = 350;
        int32_t _touch_max_sample_spread  = 40;
        int32_t _touch_samples            = 5;
        bool    _touch_swap_xy            = true;
        bool    _touch_invert_x           = true;
        bool    _touch_invert_y           = false;

        int32_t _lcd_clock_hz   = 40000000;
        int32_t _touch_clock_hz = 2000000;
        bool    _invert_display = false;

        // Chip selects, data/command and the two optional lines. The bus behind
        // them is the machine's shared spi: section, opened by Machine::SPIBus.
        Pin  _tft_cs_pin;
        Pin  _tft_dc_pin;
        Pin  _tft_reset_pin;
        Pin  _touch_cs_pin;
        Pin  _backlight_pin;
        bool _backlight_active_low = true;

        bool _configuration_valid = true;
        bool _registered          = false;
        bool _display_ready       = false;

        TS35DisplayDriver _display;
        TS35Model         _model;
        std::string       _report_line;
        bool              _discarding_report = false;

        // Selected on the panel, seeded from _jog_step_mm and _jog_percent. Kept
        // apart from those so that $CD does not save the screen's current pick.
        float   _jog_step  = 0.1f;
        int32_t _jog_speed = 80;

        Page     _page           = Page::Dro;
        bool     _page_dirty     = true;
        bool     _layout_dirty   = true;
        bool     _clearing       = false;
        int16_t  _clear_y        = 0;
        uint32_t _last_render_ms = 0;
        uint32_t _last_touch_ms  = 0;

        std::string                _rendered_header;
        uint16_t                   _rendered_header_color = White;
        std::array<std::string, 3> _rendered_axes;
        std::string                _rendered_footer;
        std::string                _rendered_step;
        std::string                _rendered_step_z;
        std::string                _rendered_speed;
        std::string                _rendered_zero_hint;
        std::string                _rendered_overrides;
        std::array<std::string, 6> _rendered_info;
        std::string                _rendered_notice;
        std::string                _rendered_alarm;

        bool     _touch_down       = false;
        int16_t  _touch_start_x    = 0;
        int16_t  _touch_start_y    = 0;
        int16_t  _last_touch_x     = -1;
        int16_t  _last_touch_y     = -1;
        uint16_t _last_touch_raw_x = 0;
        uint16_t _last_touch_raw_y = 0;
        uint16_t _last_touch_z     = 0;

        AlarmClear _alarm_clear        = AlarmClear::None;
        uint32_t   _alarm_clear_at_ms  = 0;
        bool       _alarm_banner_shown = false;
        int32_t    _last_alarm_code    = -1;

        ConfirmAction _confirm_action     = ConfirmAction::None;
        uint32_t      _confirm_expires_ms = 0;
        std::string   _notice;
        uint32_t      _notice_expires_ms = 0;
        uint32_t      _last_command_ms   = 0;
        uint32_t      _last_realtime_ms  = 0;

        void serviceTouch(uint32_t now);
        void serviceRender(uint32_t now);
        void handleTap(int16_t x, int16_t y, uint32_t now);
        void handleJogTap(int16_t x, int16_t y, uint32_t now);
        void handleZeroTap(int16_t x, int16_t y, uint32_t now);
        void handleControlTap(int16_t x, int16_t y, uint32_t now);

        void beginScreenClear();
        void clearNextBand();
        void renderPageFurniture();
        void renderHeader();
        void renderDro();
        void renderJog();
        void renderZero(uint32_t now);
        void renderControl();
        void renderInfo();
        void renderNotice(uint32_t now);
        bool renderAlarmBanner();
        void serviceAlarmClear(uint32_t now);
        bool handleAlarmBannerTap(int16_t x, int16_t y, uint32_t now);
        void drawButton(int16_t x, int16_t y, int16_t width, int16_t height, const char* label, bool enabled, bool selected = false);
        void drawCachedText(std::string& rendered,
                            const char*  text,
                            size_t       characters,
                            int16_t      x,
                            int16_t      y,
                            uint16_t     foreground,
                            uint16_t     background,
                            uint8_t      scale);
        void resetPageRenderCache();

        bool controlsAvailable(uint32_t now);
        bool enqueueLine(const std::string& command, uint32_t now, bool allowAlarm = false);
        bool sendRealtime(uint8_t command, uint32_t now);
        void requestConfirmation(ConfirmAction action, const char* label, uint32_t now);
        void executeConfirmed(ConfirmAction action, uint32_t now);
        void setNotice(const char* text, uint32_t now, uint32_t durationMs = 2500);
        void clearConfirmation();

        static bool        hit(int16_t x, int16_t y, int16_t left, int16_t top, int16_t width, int16_t height);
        static uint16_t    stateColor(MachineState state);
        static const char* pageName(Page page);
        static const char* unitsName(Units units);
    };

}  // namespace ts35
