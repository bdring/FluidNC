#include "TS35Module.h"

#include "Configuration/HandlerBase.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "RealtimeCmd.h"
#include "Serial.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ts35 {

    namespace {

        constexpr uint32_t ConfirmationWindowMs = 3000;
        constexpr uint32_t CommandCooldownMs    = 350;
        constexpr uint32_t RealtimeCooldownMs   = 90;

        // Gap between the soft reset and the $X of the UNLOCK button. A FluidNC soft
        // reset does not restart the ESP32, but it does discard channel input; sending
        // the $X together with it would only get it thrown away in the flush.
        constexpr uint32_t AlarmClearGapMs = 1200;

        const char* alarmHint(int32_t code) {
            switch (code) {
                case 1:
                    return "HARD LIMIT";
                case 2:
                    return "SOFT LIMIT";
                case 3:
                    return "ABORT DURING MOTION";
                case 4:
                case 5:
                    return "PROBE FAILED";
                case 6:
                case 7:
                case 8:
                case 9:
                    return "HOMING FAILED";
                case 10:
                    return "SPINDLE CONTROL";
                default:
                    return "";
            }
        }

        // Ladder 0.01 -> 0.1 -> 1 -> 10 -> 100 mm, truncated by jog_step_max_mm. A
        // jog_step_mm that is not on the ladder lands on the next rung above it.
        // The step is formatted in four characters so all five rungs are the same
        // width on screen, which means dropping the leading zero: ".010", not "0.010".
        void formatJogStep(char* out, size_t length, float step) {
            if (step < 0.995f) {
                std::snprintf(out, length, ".%03d", static_cast<int>(step * 1000.0f + 0.5f));
            } else if (step < 9.995f) {
                std::snprintf(out, length, "%.2f", static_cast<double>(step));
            } else if (step < 99.95f) {
                std::snprintf(out, length, "%.1f", static_cast<double>(step));
            } else {
                std::snprintf(out, length, "%4.0f", static_cast<double>(step));
            }
        }

        // Speed as a fraction of the configured feed rate rather than in mm/min: the
        // operator thinks in "slower", not in an absolute number, and the same choice
        // then applies to both XY and Z, which have different feed rates.
        int32_t nextJogPercent(int32_t current) {
            static constexpr int32_t ladder[] = { 10, 25, 50, 80, 100 };
            for (const int32_t percent : ladder) {
                if (percent > current) {
                    return percent;
                }
            }
            return ladder[0];
        }

        float nextJogStep(float current, float maximum) {
            static constexpr float ladder[] = { 0.01f, 0.1f, 1.0f, 10.0f, 100.0f };
            for (const float step : ladder) {
                if (step > current + 1e-4f && step <= maximum + 1e-4f) {
                    return step;
                }
            }
            return ladder[0];
        }

        // The display driver needs native GPIO numbers, but the numbers come from Pin
        // objects so that a GPIO already taken by a step, dir or limit pin is reported
        // as a configuration error instead of being driven behind the machine's back.
        int8_t nativeOutputPin(const Pin& pin) {
            if (pin.undefined()) {
                return -1;
            }
            return static_cast<int8_t>(pin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native));
        }

        void formatAxis(char* output, size_t length, const AxisValues& values, Axis axis) {
            if (!values.has(axis)) {
                std::snprintf(output, length, "%11s", "---.---");
                return;
            }
            const double value = values.get(axis);
            if (std::fabs(value) >= 1000000.0) {
                std::snprintf(output, length, "%11s", "OVERFLOW");
                return;
            }
            std::snprintf(output, length, "% 11.3f", value);
        }

    }  // namespace

    void TS35Module::group(Configuration::HandlerBase& handler) {
        // @config report_interval_ms
        // @default 200
        // @tuning typical
        // How often the module asks the machine for a status report. This is the
        // channel's own report interval, independent of what other channels use.
        handler.item("report_interval_ms", _report_interval_ms, 100, 5000);

        // @config render_interval_ms
        // @default 200
        // @tuning typical
        // Lower bound between screen repaints. Only fields whose text actually
        // changed are redrawn, so raising this costs responsiveness, not bandwidth.
        handler.item("render_interval_ms", _render_interval_ms, 100, 2000);

        // @config touch_interval_ms
        // @default 40
        // @tuning typical
        // How often the touch controller is sampled. Below about 20 ms the SPI
        // traffic starts competing with the display writes for the same bus.
        handler.item("touch_interval_ms", _touch_interval_ms, 20, 500);

        // @config controls_enabled
        // @default false
        // @tuning per-machine
        // Master gate for every control that moves the machine or changes its
        // coordinates. Leave it false until the touch calibration below has been
        // verified on the bench: an uncalibrated panel will send taps to the wrong
        // button.
        handler.item("controls_enabled", _controls_enabled);

        // @config allow_jog
        // @default true
        // @tuning per-machine
        // Allows the jog buttons, subject to controls_enabled.
        handler.item("allow_jog", _allow_jog);

        // @config allow_zero
        // @default true
        // @tuning per-machine
        // Allows setting work zero from the screen, subject to controls_enabled.
        // Zeroing uses G10 L20 P0, so it updates the active coordinate system and
        // shows up on the other channels at the next report.
        handler.item("allow_zero", _allow_zero);

        // @config allow_homing
        // @default false
        // @tuning per-machine
        // Allows starting a homing cycle from the screen.
        handler.item("allow_homing", _allow_homing);

        // @config allow_unlock
        // @default false
        // @tuning per-machine
        // Allows $X from the Control page. Not needed when allow_alarm_clear is
        // true, which offers the same thing where the alarm is actually shown.
        handler.item("allow_unlock", _allow_unlock);

        // @config allow_soft_reset
        // @default false
        // @tuning per-machine
        // Allows sending a soft reset from the Control page.
        handler.item("allow_soft_reset", _allow_soft_reset);

        // @config allow_alarm_clear
        // @default false
        // @tuning per-machine
        // Shows an alarm banner with an unlock button on every page whenever the
        // machine is in Alarm. The button sends a soft reset and then $X. This is
        // deliberately independent of controls_enabled, because neither command
        // produces motion, so a machine can be recovered from an alarm while the
        // screen is otherwise read-only.
        handler.item("allow_alarm_clear", _allow_alarm_clear);

        // @config jog_step_mm
        // @default 0.100
        // @tuning per-machine
        // Starting jog increment. The STEP button cycles it through 0.01, 0.1, 1,
        // 10 and 100 mm, capped as described under jog_step_max_mm; this value only
        // picks where that ladder starts. The screen's own selection is not written
        // back here, so $CD still reports what the config file asked for.
        handler.item("jog_step_mm", _jog_step_mm, 0.001f, 100.0f);

        // @config jog_feed_xy_mm_min
        // @default 1000.000
        // @tuning per-machine
        // Feed rate used for jogs on X and Y.
        handler.item("jog_feed_xy_mm_min", _jog_feed_xy_mm_min, 1.0f, 100000.0f);

        // @config jog_feed_z_mm_min
        // @default 500.000
        // @tuning per-machine
        // Feed rate used for jogs on Z.
        handler.item("jog_feed_z_mm_min", _jog_feed_z_mm_min, 1.0f, 100000.0f);

        // @config jog_step_max_mm
        // @default 10.000
        // @tuning per-machine
        // Largest jog increment applied to X and Y. The STEP button cycles the
        // ladder 0.01, 0.1, 1, 10, 100 mm up to whichever of the two caps is
        // higher, then wraps back to 0.01.
        handler.item("jog_step_max_mm", _jog_step_max_mm, 0.01f, 100.0f);

        // @config jog_percent
        // @default 80
        // @tuning typical
        // Starting speed for the jog buttons, as a percentage of the two feed
        // rates above. The SPEED button cycles it through 10, 25, 50, 80 and 100,
        // so those feeds are the ceiling rather than the working value; set them to
        // what the machine can actually sustain and steer from the panel. As with
        // jog_step_mm, the screen's selection stays out of the saved config.
        handler.item("jog_percent", _jog_percent, 1, 100);

        // @config jog_step_max_z_mm
        // @default 10.000
        // @tuning per-machine
        // Same, for Z. It is separate because Z usually has an order of magnitude
        // less travel than X and Y, and with soft_limits off an increment longer
        // than the travel that is left drives the axis into its hard limit. When
        // the selected step is above this cap, a Z jog moves this much instead and
        // the jog page says so.
        handler.item("jog_step_max_z_mm", _jog_step_max_z_mm, 0.01f, 100.0f);

        // @config touch_x_min
        // @default 300
        // @tuning per-machine
        // Raw ADC reading at the left edge of the panel. Read the raw values from
        // the Info page while touching the edges and put them here.
        handler.item("touch_x_min", _touch_x_min, 0, 4095);

        // @config touch_x_max
        // @default 3600
        // @tuning per-machine
        // Raw ADC reading at the right edge of the panel.
        handler.item("touch_x_max", _touch_x_max, 0, 4095);

        // @config touch_y_min
        // @default 300
        // @tuning per-machine
        // Raw ADC reading at the top edge of the panel.
        handler.item("touch_y_min", _touch_y_min, 0, 4095);

        // @config touch_y_max
        // @default 3600
        // @tuning per-machine
        // Raw ADC reading at the bottom edge of the panel.
        handler.item("touch_y_max", _touch_y_max, 0, 4095);

        // @config touch_pressure_threshold
        // @default 350
        // @tuning per-machine
        // Minimum pressure reading for a sample to count as a touch. Raise it if
        // the screen registers phantom taps, lower it if firm taps are missed.
        handler.item("touch_pressure_threshold", _touch_pressure_threshold, 0, 4095);

        // @config touch_max_sample_spread
        // @default 40
        // @tuning typical
        // Rejects a reading whose samples disagree by more than this, which is what
        // a finger sliding off the panel looks like.
        handler.item("touch_max_sample_spread", _touch_max_sample_spread, 0, 4095);

        // @config touch_samples
        // @default 5
        // @tuning typical
        // Samples per reading, median filtered. Odd values give a true median.
        handler.item("touch_samples", _touch_samples, 1, 9);

        // @config touch_swap_xy
        // @default true
        // @tuning per-machine
        // Swaps the panel's two axes before mapping them to the screen. Needed when
        // the resistive panel is wired at ninety degrees to the LCD scan order.
        handler.item("touch_swap_xy", _touch_swap_xy);

        // @config touch_invert_x
        // @default true
        // @tuning per-machine
        // Mirrors the horizontal axis of the touch panel.
        handler.item("touch_invert_x", _touch_invert_x);

        // @config touch_invert_y
        // @default false
        // @tuning per-machine
        // Mirrors the vertical axis of the touch panel.
        handler.item("touch_invert_y", _touch_invert_y);

        // @config lcd_clock_hz
        // @default 40000000
        // @tuning typical
        // SPI clock for the display. Reduce it if a long or poorly seated ribbon
        // cable produces visible corruption.
        handler.item("lcd_clock_hz", _lcd_clock_hz, 1000000, 40000000);

        // @config touch_clock_hz
        // @default 2000000
        // @tuning typical
        // SPI clock for the touch controller. The ADS7843/XPT2046 is specified well
        // below the display's clock and should not be run at the same rate.
        handler.item("touch_clock_hz", _touch_clock_hz, 100000, 2000000);

        // @config invert_display
        // @default false
        // @tuning per-machine
        // Sends the panel's colour inversion command at startup. Some ST7796 panels
        // ship inverted relative to the reference module.
        handler.item("invert_display", _invert_display);

        // @config tft_cs_pin
        // @default NO_PIN
        // @tuning per-machine
        // @pin_attributes output
        // Chip select for the display controller. Must be a native MCU pin with
        // output capability; a pin extender cannot keep up with the bus. Required.
        // The SPI bus itself (sck/mosi/miso) is the machine's shared spi: section,
        // same as SDCard.
        handler.item("tft_cs_pin", _tft_cs_pin);

        // @config tft_dc_pin
        // @default NO_PIN
        // @tuning per-machine
        // @pin_attributes output
        // Data/command line of the display controller. Must be a native MCU pin
        // with output capability. Required.
        handler.item("tft_dc_pin", _tft_dc_pin);

        // @config touch_cs_pin
        // @default NO_PIN
        // @tuning per-machine
        // @pin_attributes output
        // Chip select for the touch controller, which shares the bus with the
        // display. Must be a native MCU pin with output capability. Required.
        handler.item("touch_cs_pin", _touch_cs_pin);

        // @config tft_reset_pin
        // @default NO_PIN
        // @tuning per-machine
        // @pin_attributes output
        // Hardware reset line of the display controller. Optional: leave it out on
        // a panel whose reset is tied to the board's own reset.
        handler.item("tft_reset_pin", _tft_reset_pin);

        // @config backlight_pin
        // @default NO_PIN
        // @tuning per-machine
        // @pin_attributes output
        // Switches the backlight. Optional: leave it out on a panel that is lit
        // whenever it is powered.
        handler.item("backlight_pin", _backlight_pin);

        // @config backlight_active_low
        // @default true
        // @tuning per-machine
        // Set when the backlight pin is pulled low to light the panel. Boards differ
        // here, sometimes within the same model, so this is worth checking on the
        // bench before assuming the display is dead.
        handler.item("backlight_active_low", _backlight_active_low);
    }

    void TS35Module::validate() {
        if (_touch_x_min >= _touch_x_max || _touch_y_min >= _touch_y_max) {
            log_error("TS35 touch min must be less than max");
            _configuration_valid = false;
        }
        if ((_touch_samples % 2) == 0) {
            log_warn("TS35 touch_samples should be odd for median filtering");
        }
        // Pin objects claim their GPIO as they are parsed, so a collision with a
        // step, dir or limit pin is already an error by the time we get here. What
        // is left to check is that the three mandatory ones were given at all.
        if (_tft_cs_pin.undefined() || _tft_dc_pin.undefined() || _touch_cs_pin.undefined()) {
            log_error("TS35 needs tft_cs_pin, tft_dc_pin and touch_cs_pin");
            _configuration_valid = false;
        }
    }

    void TS35Module::afterParse() {
        // The two jog knobs are edited from the panel, so the running values are
        // kept apart from the configured ones; $CD then still writes out the config
        // file's numbers rather than whatever was last selected on screen.
        _jog_step  = _jog_step_mm;
        _jog_speed = _jog_percent;
        if (_controls_enabled) {
            log_warn("TS35 controls enabled; validate touch calibration before powering motors");
        }
    }

    void TS35Module::init() {
        if (!_configuration_valid) {
            return;
        }

        // The panel hangs off the machine's shared SPI bus, which Main.cpp opens
        // from the spi: section well before any module is initialised.
        if (config->_spi == nullptr || !config->_spi->defined()) {
            log_error("TS35 needs an spi: section with sck_pin, miso_pin and mosi_pin");
            return;
        }

        TS35DisplayDriver::Config displayConfig;
        displayConfig.pins.tftCs              = nativeOutputPin(_tft_cs_pin);
        displayConfig.pins.tftDc              = nativeOutputPin(_tft_dc_pin);
        displayConfig.pins.tftReset           = nativeOutputPin(_tft_reset_pin);
        displayConfig.pins.touchCs            = nativeOutputPin(_touch_cs_pin);
        displayConfig.pins.backlight          = nativeOutputPin(_backlight_pin);
        displayConfig.touch.xMin              = static_cast<uint16_t>(_touch_x_min);
        displayConfig.touch.xMax              = static_cast<uint16_t>(_touch_x_max);
        displayConfig.touch.yMin              = static_cast<uint16_t>(_touch_y_min);
        displayConfig.touch.yMax              = static_cast<uint16_t>(_touch_y_max);
        displayConfig.touch.pressureThreshold = static_cast<uint16_t>(_touch_pressure_threshold);
        displayConfig.touch.maxSampleSpread   = static_cast<uint16_t>(_touch_max_sample_spread);
        displayConfig.touch.samples           = static_cast<uint8_t>(_touch_samples);
        displayConfig.touch.swapXY            = _touch_swap_xy;
        displayConfig.touch.invertX           = _touch_invert_x;
        displayConfig.touch.invertY           = _touch_invert_y;
        displayConfig.lcdClockHz              = static_cast<uint32_t>(_lcd_clock_hz);
        displayConfig.touchClockHz            = static_cast<uint32_t>(_touch_clock_hz);
        displayConfig.invertDisplay           = _invert_display;
        displayConfig.backlightActiveLow      = _backlight_active_low;

        _display.setConfig(displayConfig);
        if (!_display.begin()) {
            log_error("TS35 display initialization failed");
            return;
        }

        _display_ready = true;
        _display.drawText(132, 116, "FLUIDNC TS35", White, Black, 3);
        _display.drawText(192, 154, "STARTING", Cyan, Black, 2);

        allChannels.registration(this);
        _registered = true;
        setReportInterval(static_cast<uint32_t>(_report_interval_ms));

        log_info("TS35 touchscreen channel, controls " << (_controls_enabled ? "enabled" : "read-only"));
    }

    void TS35Module::deinit() {
        if (_registered) {
            allChannels.deregistration(this);
            _registered = false;
        }
        if (_display_ready) {
            _display.end();
            _display_ready = false;
        }
    }

    size_t TS35Module::write(uint8_t data) {
        const char character = static_cast<char>(data);
        if (character == '\r') {
            return 1;
        }
        if (character == '\n') {
            if (!_discarding_report && !_report_line.empty()) {
                Reply reply;
                if (_model.parseLine(_report_line, &reply)) {
                    if (reply.kind == ReplyKind::Alarm) {
                        _last_alarm_code = reply.hasCode ? reply.code : -1;
                    }
                    _page_dirty = _page_dirty || reply.kind == ReplyKind::Modal || reply.kind == ReplyKind::Alarm;
                }
            }
            _report_line.clear();
            _discarding_report = false;
            return 1;
        }
        if (_discarding_report) {
            return 1;
        }
        if (_report_line.size() >= 1024) {
            _report_line.clear();
            _discarding_report = true;
            return 1;
        }
        _report_line.push_back(character);
        return 1;
    }

    Error TS35Module::pollLine(char* line) {
        autoReport();
        if (_display_ready) {
            const uint32_t now = millis();
            serviceAlarmClear(now);
            serviceTouch(now);
            serviceRender(now);
        }
        return Channel::pollLine(line);
    }

    void TS35Module::serviceTouch(uint32_t now) {
        if (now - _last_touch_ms < static_cast<uint32_t>(_touch_interval_ms)) {
            return;
        }
        _last_touch_ms = now;

        int16_t    x       = 0;
        int16_t    y       = 0;
        const bool pressed = _display.readTouch(x, y);

        if (_page == Page::Info) {
            uint16_t rawX = 0;
            uint16_t rawY = 0;
            uint16_t rawZ = 0;
            if (_display.readTouchRaw(rawX, rawY, &rawZ)) {
                _last_touch_raw_x = rawX;
                _last_touch_raw_y = rawY;
                _last_touch_z     = rawZ;
            }
        }

        if (pressed) {
            _last_touch_x = x;
            _last_touch_y = y;
            if (!_touch_down) {
                _touch_down    = true;
                _touch_start_x = x;
                _touch_start_y = y;
            }
            return;
        }

        if (_touch_down) {
            _touch_down            = false;
            const int16_t releaseX = _last_touch_x;
            const int16_t releaseY = _last_touch_y;
            if (std::abs(releaseX - _touch_start_x) <= 35 && std::abs(releaseY - _touch_start_y) <= 35) {
                handleTap(releaseX, releaseY, now);
            }
        }
    }

    void TS35Module::handleTap(int16_t x, int16_t y, uint32_t now) {
        if (handleAlarmBannerTap(x, y, now)) {
            return;
        }
        if (y >= NavTop) {
            const int pageIndex = std::max(0, std::min(PageCount - 1, static_cast<int>(x / NavWidth)));
            if (static_cast<Page>(pageIndex) != _page) {
                _page         = static_cast<Page>(pageIndex);
                _layout_dirty = true;
            }
            _page_dirty = true;
            clearConfirmation();
            return;
        }

        switch (_page) {
            case Page::Jog:
                handleJogTap(x, y, now);
                break;
            case Page::Zero:
                handleZeroTap(x, y, now);
                break;
            case Page::Control:
                handleControlTap(x, y, now);
                break;
            case Page::Dro:
            case Page::Info:
                break;
        }
    }

    bool TS35Module::controlsAvailable(uint32_t now) {
        if (!_controls_enabled) {
            setNotice("CONTROLS DISABLED IN YAML", now);
            return false;
        }
        return true;
    }

    void TS35Module::handleJogTap(int16_t x, int16_t y, uint32_t now) {
        // Step and speed move nothing, so they stay adjustable even while the
        // controls are locked: the jog can be set up before it is unlocked.
        if (hit(x, y, JogStepX, JogActionY, JogStepW, JogActionH)) {
            _jog_step = nextJogStep(_jog_step, std::max(_jog_step_max_mm, _jog_step_max_z_mm));
            return;
        }
        if (hit(x, y, JogSpeedX, JogActionY, JogSpeedW, JogActionH)) {
            _jog_speed = nextJogPercent(_jog_speed);
            return;
        }
        if (!controlsAvailable(now) || !_allow_jog) {
            return;
        }

        Axis   axis     = Axis::X;
        double distance = 0.0;
        double feed     = _jog_feed_xy_mm_min;
        bool   selected = true;

        if (hit(x, y, JogLeftX, JogRowY[0], JogBtnW, JogBtnH)) {
            axis     = Axis::X;
            distance = -_jog_step;
        } else if (hit(x, y, JogRightX, JogRowY[0], JogBtnW, JogBtnH)) {
            axis     = Axis::X;
            distance = _jog_step;
        } else if (hit(x, y, JogLeftX, JogRowY[1], JogBtnW, JogBtnH)) {
            axis     = Axis::Y;
            distance = -_jog_step;
        } else if (hit(x, y, JogRightX, JogRowY[1], JogBtnW, JogBtnH)) {
            axis     = Axis::Y;
            distance = _jog_step;
        } else if (hit(x, y, JogLeftX, JogRowY[2], JogBtnW, JogBtnH)) {
            axis     = Axis::Z;
            distance = -_jog_step;
            feed     = _jog_feed_z_mm_min;
        } else if (hit(x, y, JogRightX, JogRowY[2], JogBtnW, JogBtnH)) {
            axis     = Axis::Z;
            distance = _jog_step;
            feed     = _jog_feed_z_mm_min;
        } else if (hit(x, y, JogCancelX, JogActionY, JogCancelW, JogActionH)) {
            sendRealtime(TS35Model::realtimeByte(RealtimeAction::CancelJog), now);
            return;
        } else {
            selected = false;
        }

        if (selected && distance != 0.0) {
            // The step is shared by all six buttons, but the travel is not. Cap it
            // by the requested axis; renderJog() shows the effective Z value so the
            // reduction is never silent.
            const double cap       = axis == Axis::Z ? _jog_step_max_z_mm : _jog_step_max_mm;
            const double magnitude = std::min(std::fabs(distance), cap);
            distance               = distance < 0.0 ? -magnitude : magnitude;

            // The percentage applies to both axes. The floor of 1 avoids a jog with
            // a zero feed rate, which the firmware would reject.
            feed = std::max(1.0, feed * static_cast<double>(_jog_speed) / 100.0);
        }

        if (selected) {
            std::vector<AxisMove> moves;
            moves.push_back(AxisMove(axis, distance));
            const CommandBuildResult command = buildIncrementalJogCommand(moves, feed);
            if (command.ok) {
                enqueueLine(command.command, now);
            }
        }
    }

    void TS35Module::handleZeroTap(int16_t x, int16_t y, uint32_t now) {
        if (!controlsAvailable(now) || !_allow_zero) {
            return;
        }
        if (hit(x, y, ZeroX[0], ZeroY[0], ZeroBtnW, ZeroBtnH)) {
            requestConfirmation(ConfirmAction::ZeroX, "ZERO X", now);
        } else if (hit(x, y, ZeroX[1], ZeroY[0], ZeroBtnW, ZeroBtnH)) {
            requestConfirmation(ConfirmAction::ZeroY, "ZERO Y", now);
        } else if (hit(x, y, ZeroX[0], ZeroY[1], ZeroBtnW, ZeroBtnH)) {
            requestConfirmation(ConfirmAction::ZeroZ, "ZERO Z", now);
        } else if (hit(x, y, ZeroX[1], ZeroY[1], ZeroBtnW, ZeroBtnH)) {
            requestConfirmation(ConfirmAction::ZeroXYZ, "ZERO XYZ", now);
        }
    }

    void TS35Module::handleControlTap(int16_t x, int16_t y, uint32_t now) {
        if (!controlsAvailable(now)) {
            return;
        }

        const ModelSnapshot& snapshot = _model.snapshot();
        if (hit(x, y, CtrlWideX[0], CtrlTopY, CtrlWideW, CtrlTopH)) {
            sendRealtime(TS35Model::realtimeByte(RealtimeAction::FeedHold), now);
        } else if (hit(x, y, CtrlWideX[1], CtrlTopY, CtrlWideW, CtrlTopH)) {
            sendRealtime(TS35Model::realtimeByte(RealtimeAction::Resume), now);
        } else if (hit(x, y, CtrlWideX[2], CtrlTopY, CtrlWideW, CtrlTopH)) {
            sendRealtime(TS35Model::realtimeByte(RealtimeAction::CancelJog), now);
        } else if (hit(x, y, CtrlOvrX[0], CtrlOvrY, CtrlOvrW, CtrlOvrH)) {
            sendRealtime(static_cast<uint8_t>(Cmd::FeedOvrCoarseMinus), now);
        } else if (hit(x, y, CtrlOvrX[1], CtrlOvrY, CtrlOvrW, CtrlOvrH)) {
            sendRealtime(static_cast<uint8_t>(Cmd::FeedOvrReset), now);
        } else if (hit(x, y, CtrlOvrX[2], CtrlOvrY, CtrlOvrW, CtrlOvrH)) {
            sendRealtime(static_cast<uint8_t>(Cmd::FeedOvrCoarsePlus), now);
        } else if (hit(x, y, CtrlRapidX, CtrlOvrY, CtrlRapidW, CtrlOvrH)) {
            sendRealtime(static_cast<uint8_t>(Cmd::RapidOvrReset), now);
        } else if (hit(x, y, CtrlWideX[0], CtrlBotY, CtrlWideW, CtrlBotH) && _allow_homing && snapshot.state == MachineState::Idle) {
            requestConfirmation(ConfirmAction::Home, "HOME", now);
        } else if (hit(x, y, CtrlWideX[1], CtrlBotY, CtrlWideW, CtrlBotH) && _allow_unlock && snapshot.state == MachineState::Alarm) {
            requestConfirmation(ConfirmAction::Unlock, "UNLOCK", now);
        } else if (hit(x, y, CtrlWideX[2], CtrlBotY, CtrlWideW, CtrlBotH) && _allow_soft_reset) {
            requestConfirmation(ConfirmAction::SoftReset, "SOFT RESET", now);
        }
    }

    void TS35Module::requestConfirmation(ConfirmAction action, const char* label, uint32_t now) {
        if (_confirm_action == action && static_cast<int32_t>(_confirm_expires_ms - now) > 0) {
            clearConfirmation();
            executeConfirmed(action, now);
            return;
        }
        _confirm_action     = action;
        _confirm_expires_ms = now + ConfirmationWindowMs;
        std::string prompt("TOUCH AGAIN: ");
        prompt += label;
        setNotice(prompt.c_str(), now, ConfirmationWindowMs);
    }

    void TS35Module::executeConfirmed(ConfirmAction action, uint32_t now) {
        std::vector<Axis> axes;
        switch (action) {
            case ConfirmAction::ZeroX:
                axes.push_back(Axis::X);
                break;
            case ConfirmAction::ZeroY:
                axes.push_back(Axis::Y);
                break;
            case ConfirmAction::ZeroZ:
                axes.push_back(Axis::Z);
                break;
            case ConfirmAction::ZeroXYZ:
                axes.push_back(Axis::X);
                axes.push_back(Axis::Y);
                axes.push_back(Axis::Z);
                break;
            case ConfirmAction::Home:
                enqueueLine("$H", now);
                return;
            case ConfirmAction::Unlock:
                enqueueLine("$X", now, true);
                return;
            case ConfirmAction::SoftReset:
                sendRealtime(TS35Model::realtimeByte(RealtimeAction::EmergencyReset), now);
                return;
            case ConfirmAction::None:
                return;
        }

        const CommandBuildResult command = buildSetWorkZeroCommand(axes);
        if (command.ok && enqueueLine(command.command, now)) {
            setNotice("ZERO SENT; WAITING FOR WCO", now, 3000);
        }
    }

    bool TS35Module::enqueueLine(const std::string& command, uint32_t now, bool allowAlarm) {
        if (!_controls_enabled || now - _last_command_ms < CommandCooldownMs) {
            return false;
        }
        const ModelSnapshot& snapshot = _model.snapshot();
        if (allowAlarm) {
            if (command != "$X" || snapshot.state != MachineState::Alarm) {
                setNotice("COMMAND BLOCKED BY MACHINE STATE", now);
                return false;
            }
        } else if (!_model.canSendLineCommand(command)) {
            setNotice("COMMAND REQUIRES IDLE", now);
            return false;
        }
        push(command);
        push(static_cast<uint8_t>('\n'));
        _last_command_ms = now;
        setNotice("COMMAND SENT", now);
        return true;
    }

    bool TS35Module::sendRealtime(uint8_t command, uint32_t now) {
        if (!_controls_enabled || now - _last_realtime_ms < RealtimeCooldownMs) {
            return false;
        }

        bool allowed = false;
        if (command >= static_cast<uint8_t>(Cmd::FeedOvrReset) && command <= static_cast<uint8_t>(Cmd::CoolantMistOvrToggle)) {
            // The override commands apply in any state the controller has reported.
            allowed = _model.snapshot().state != MachineState::Unknown && _model.snapshot().state != MachineState::Starting;
        } else if (command == TS35Model::realtimeByte(RealtimeAction::EmergencyReset)) {
            allowed = _allow_soft_reset && _model.canSendRealtime(RealtimeAction::EmergencyReset);
        } else {
            const std::array<RealtimeAction, 3> actions = { RealtimeAction::FeedHold, RealtimeAction::Resume, RealtimeAction::CancelJog };
            for (const RealtimeAction action : actions) {
                if (command == TS35Model::realtimeByte(action)) {
                    allowed = _model.canSendRealtime(action);
                    break;
                }
            }
        }
        if (!allowed) {
            setNotice("CONTROL DOES NOT APPLY IN THIS STATE", now);
            return false;
        }
        push(command);
        _last_realtime_ms = now;
        setNotice("REALTIME SENT", now, 1200);
        return true;
    }

    void TS35Module::serviceRender(uint32_t now) {
        bool layoutJustDrawn = false;
        if (_layout_dirty || _clearing) {
            if (!_clearing) {
                beginScreenClear();
            }
            clearNextBand();
            if (_clearing) {
                return;  // nothing may be drawn on top of a half-wiped screen
            }
            renderPageFurniture();
            _layout_dirty   = false;
            layoutJustDrawn = true;
        }

        if (!layoutJustDrawn && !_page_dirty && now - _last_render_ms < static_cast<uint32_t>(_render_interval_ms)) {
            return;
        }
        _page_dirty     = false;
        _last_render_ms = now;

        renderHeader();
        if (renderAlarmBanner()) {
            // The banner is an overlay across the middle of the screen. The page
            // body and the notice line both draw inside the band it occupies, so
            // while it is up they are left alone; dropping it repaints the page.
            return;
        }
        switch (_page) {
            case Page::Dro:
                renderDro();
                break;
            case Page::Jog:
                renderJog();
                break;
            case Page::Zero:
                renderZero(now);
                break;
            case Page::Control:
                renderControl();
                break;
            case Page::Info:
                renderInfo();
                break;
        }
        renderNotice(now);
    }

    void TS35Module::beginScreenClear() {
        _clearing           = true;
        _clear_y            = 0;
        _alarm_banner_shown = false;
        _rendered_header.clear();
        _rendered_notice.clear();
        _rendered_alarm.clear();
        resetPageRenderCache();
    }

    // Wiping 480x320 at 16 bpp is 307200 bytes on the wire, tens of milliseconds
    // that pollLine() would spend inside the shared channel polling loop. The wipe
    // is therefore spread over consecutive polls, one band at a time, and nothing
    // else is drawn until the last band is done.
    void TS35Module::clearNextBand() {
        const int16_t height = std::min<int16_t>(ClearBandHeight, static_cast<int16_t>(ScreenHeight - _clear_y));
        _display.fillRect(0, _clear_y, ScreenWidth, height, Black);
        _clear_y = static_cast<int16_t>(_clear_y + height);
        if (_clear_y >= ScreenHeight) {
            _clearing = false;
        }
    }

    void TS35Module::renderPageFurniture() {
        for (int page = 0; page < PageCount; ++page) {
            const bool selected = static_cast<int>(_page) == page;
            drawButton(page * NavWidth, NavTop, NavWidth, NavHeight, pageName(static_cast<Page>(page)), true, selected);
        }

        switch (_page) {
            case Page::Dro:
                _display.drawText(16, 58, "X", Cyan, Black, 4);
                _display.drawText(16, 124, "Y", Cyan, Black, 4);
                _display.drawText(16, 190, "Z", Cyan, Black, 4);
                break;
            case Page::Jog: {
                const bool               jogOk     = _controls_enabled && _allow_jog;
                static const char* const minus[3]  = { "X -", "Y -", "Z -" };
                static const char* const plus[3]   = { "X +", "Y +", "Z +" };
                static const char* const letter[3] = { "X", "Y", "Z" };
                for (int row = 0; row < 3; ++row) {
                    drawButton(JogLeftX, JogRowY[row], JogBtnW, JogBtnH, minus[row], jogOk);
                    drawButton(JogRightX, JogRowY[row], JogBtnW, JogBtnH, plus[row], jogOk);
                    _display.drawText(JogReadLetterX, JogRowY[row] + 15, letter[row], Cyan, Black, 2);
                }
                // CANCEL sits in the middle: it is the button reached for in a
                // hurry, and the middle is where a thumb lands without aiming.
                drawButton(JogCancelX, JogActionY, JogCancelW, JogActionH, "CANCEL", jogOk);
            } break;
            case Page::Zero: {
                const bool zeroOk = _controls_enabled && _allow_zero;
                drawButton(ZeroX[0], ZeroY[0], ZeroBtnW, ZeroBtnH, "ZERO X", zeroOk);
                drawButton(ZeroX[1], ZeroY[0], ZeroBtnW, ZeroBtnH, "ZERO Y", zeroOk);
                drawButton(ZeroX[0], ZeroY[1], ZeroBtnW, ZeroBtnH, "ZERO Z", zeroOk);
                drawButton(ZeroX[1], ZeroY[1], ZeroBtnW, ZeroBtnH, "ZERO XYZ", zeroOk);
            } break;
            case Page::Control:
                drawButton(CtrlWideX[0], CtrlTopY, CtrlWideW, CtrlTopH, "HOLD", _controls_enabled);
                drawButton(CtrlWideX[1], CtrlTopY, CtrlWideW, CtrlTopH, "RESUME", _controls_enabled);
                drawButton(CtrlWideX[2], CtrlTopY, CtrlWideW, CtrlTopH, "CANCEL JOG", _controls_enabled);
                drawButton(CtrlOvrX[0], CtrlOvrY, CtrlOvrW, CtrlOvrH, "F -10", _controls_enabled);
                drawButton(CtrlOvrX[1], CtrlOvrY, CtrlOvrW, CtrlOvrH, "F 100", _controls_enabled);
                drawButton(CtrlOvrX[2], CtrlOvrY, CtrlOvrW, CtrlOvrH, "F +10", _controls_enabled);
                drawButton(CtrlRapidX, CtrlOvrY, CtrlRapidW, CtrlOvrH, "R 100", _controls_enabled);
                drawButton(CtrlWideX[0], CtrlBotY, CtrlWideW, CtrlBotH, "HOME", _controls_enabled && _allow_homing);
                drawButton(CtrlWideX[1], CtrlBotY, CtrlWideW, CtrlBotH, "UNLOCK", _controls_enabled && _allow_unlock);
                drawButton(CtrlWideX[2], CtrlBotY, CtrlWideW, CtrlBotH, "RESET", _controls_enabled && _allow_soft_reset);
                break;
            case Page::Info:
                break;
        }
    }

    void TS35Module::renderHeader() {
        const ModelSnapshot& snapshot = _model.snapshot();
        const uint16_t       color    = stateColor(snapshot.state);
        char                 title[48];
        std::snprintf(title,
                      sizeof(title),
                      "FLUIDNC %-12s %-5s",
                      machineStateName(snapshot.state),
                      coordinateSystemName(snapshot.modal.coordinateSystem));
        std::string signature(title);
        signature += _controls_enabled ? "|CONTROL" : "|READONLY";
        if (_rendered_header == signature && _rendered_header_color == color) {
            return;
        }
        _rendered_header       = signature;
        _rendered_header_color = color;
        _display.fillRect(0, 0, ScreenWidth, 38, color);
        _display.drawText(10, 10, title, White, color, 2);
        if (!_controls_enabled) {
            _display.drawText(378, 10, "READ ONLY", Yellow, color, 1);
        }
    }

    void TS35Module::renderDro() {
        const ModelSnapshot&      snapshot = _model.snapshot();
        const std::array<Axis, 3> axes     = { Axis::X, Axis::Y, Axis::Z };
        for (size_t index = 0; index < axes.size(); ++index) {
            const int16_t y = static_cast<int16_t>(48 + index * 66);
            char          value[32];
            formatAxis(value, sizeof(value), snapshot.wpos, axes[index]);
            drawCachedText(_rendered_axes[index], value, 11, 82, y + 8, White, Black, 5);
        }

        char         footer[96];
        const double feed = snapshot.fs.valid ? snapshot.fs.feed : 0.0;
        const double rpm  = snapshot.fs.valid ? snapshot.fs.spindle : 0.0;
        std::snprintf(footer,
                      sizeof(footer),
                      "WPOS %s   F %.0f   S %.0f   PIN %s",
                      unitsName(snapshot.modal.units),
                      feed,
                      rpm,
                      snapshot.pins.raw.empty() ? "-" : snapshot.pins.raw.c_str());
        drawCachedText(_rendered_footer, footer, 77, 10, DroFooterY, LightGray, Black, 1);
    }

    void TS35Module::renderJog() {
        const ModelSnapshot& snapshot = _model.snapshot();
        const bool           jogOk    = _controls_enabled && _allow_jog;

        char value[32];
        for (int row = 0; row < 3; ++row) {
            static const Axis axes[3] = { Axis::X, Axis::Y, Axis::Z };
            formatAxis(value, sizeof(value), snapshot.wpos, axes[row]);
            drawCachedText(_rendered_axes[row], value, 11, JogReadValueX, JogRowY[row] + 15, White, Black, 2);
        }

        // These two buttons carry their own value, so they are redrawn when it
        // changes instead of having a separate text field beside them.
        char step[16];
        formatJogStep(step, sizeof(step), _jog_step);
        char stepLabel[24];
        std::snprintf(stepLabel, sizeof(stepLabel), "STEP %s", step);
        if (_rendered_step != stepLabel) {
            _rendered_step = stepLabel;
            drawButton(JogStepX, JogActionY, JogStepW, JogActionH, stepLabel, true);
        }

        char speedLabel[24];
        std::snprintf(speedLabel, sizeof(speedLabel), "SPEED %ld%%", static_cast<long>(_jog_speed));
        if (_rendered_speed != speedLabel) {
            _rendered_speed = speedLabel;
            drawButton(JogSpeedX, JogActionY, JogSpeedW, JogActionH, speedLabel, jogOk);
        }

        char zNote[40] = "";
        if (_jog_step > _jog_step_max_z_mm) {
            char zStep[16];
            formatJogStep(zStep, sizeof(zStep), _jog_step_max_z_mm);
            std::snprintf(zNote, sizeof(zNote), "Z CAPPED AT %s MM", zStep);
        }
        drawCachedText(_rendered_step_z, zNote, 24, JogStepX, JogNoteY, Orange, Black, 1);
    }

    void TS35Module::renderZero(uint32_t now) {
        const bool confirming = _confirm_action != ConfirmAction::None && static_cast<int32_t>(_confirm_expires_ms - now) > 0;
        drawCachedText(_rendered_zero_hint,
                       confirming ? "TOUCH AGAIN TO CONFIRM" : "ZERO USES G10 L20 P0 ON THE ACTIVE WCS",
                       74,
                       18,
                       ZeroHintY,
                       confirming ? Yellow : LightGray,
                       Black,
                       1);
    }

    void TS35Module::renderControl() {
        const ModelSnapshot& snapshot = _model.snapshot();
        char                 overrides[80];
        if (snapshot.overrides.valid) {
            std::snprintf(overrides,
                          sizeof(overrides),
                          "OVERRIDE  FEED %u%%  RAPID %u%%  SPINDLE %u%%",
                          static_cast<unsigned>(snapshot.overrides.feed),
                          static_cast<unsigned>(snapshot.overrides.rapid),
                          static_cast<unsigned>(snapshot.overrides.spindle));
        } else {
            std::snprintf(overrides, sizeof(overrides), "OVERRIDE NOT REPORTED YET");
        }
        drawCachedText(_rendered_overrides, overrides, 74, 18, CtrlOverrideY, LightGray, Black, 1);
    }

    void TS35Module::renderInfo() {
        const ModelSnapshot& snapshot = _model.snapshot();

        char line[96];
        std::snprintf(line, sizeof(line), "TOUCH MAP: X %d  Y %d", static_cast<int>(_last_touch_x), static_cast<int>(_last_touch_y));
        drawCachedText(_rendered_info[0], line, 38, 18, 58, Cyan, Black, 2);
        std::snprintf(line,
                      sizeof(line),
                      "TOUCH RAW: X %u  Y %u  Z %u",
                      static_cast<unsigned>(_last_touch_raw_x),
                      static_cast<unsigned>(_last_touch_raw_y),
                      static_cast<unsigned>(_last_touch_z));
        drawCachedText(_rendered_info[1], line, 38, 18, 88, White, Black, 2);
        std::snprintf(line,
                      sizeof(line),
                      "CAL X %ld..%ld  Y %ld..%ld",
                      static_cast<long>(_touch_x_min),
                      static_cast<long>(_touch_x_max),
                      static_cast<long>(_touch_y_min),
                      static_cast<long>(_touch_y_max));
        drawCachedText(_rendered_info[2], line, 38, 18, 124, LightGray, Black, 2);
        std::snprintf(line,
                      sizeof(line),
                      "MODAL %s  %s  %s",
                      coordinateSystemName(snapshot.modal.coordinateSystem),
                      unitsName(snapshot.modal.units),
                      snapshot.modal.distanceMode == DistanceMode::Absolute ?
                          "G90" :
                          (snapshot.modal.distanceMode == DistanceMode::Incremental ? "G91" : "?"));
        drawCachedText(_rendered_info[3], line, 38, 18, 160, White, Black, 2);
        std::snprintf(line,
                      sizeof(line),
                      "BF %u/%u  LN %u  SD %.1f%%",
                      static_cast<unsigned>(snapshot.buffers.plannerAvailable),
                      static_cast<unsigned>(snapshot.buffers.rxAvailable),
                      static_cast<unsigned>(snapshot.lineNumber.value),
                      snapshot.sd.valid ? snapshot.sd.percent : 0.0);
        drawCachedText(_rendered_info[4], line, 38, 18, 196, White, Black, 2);
        drawCachedText(_rendered_info[5],
                       _controls_enabled ? "CONTROLS: ENABLED" : "CONTROLS: DISABLED IN YAML",
                       38,
                       18,
                       234,
                       _controls_enabled ? Green : Yellow,
                       Black,
                       2);
    }

    bool TS35Module::renderAlarmBanner() {
        const bool active = _allow_alarm_clear && _model.snapshot().state == MachineState::Alarm;
        if (!active) {
            if (_alarm_banner_shown) {
                // The banner covers usable page area, so leaving the alarm means
                // the whole page has to be repainted.
                _alarm_banner_shown = false;
                _alarm_clear        = AlarmClear::None;
                _last_alarm_code    = -1;
                _rendered_alarm.clear();
                _layout_dirty = true;
                _page_dirty   = true;
            }
            return false;
        }

        char headline[32];
        if (_last_alarm_code >= 0) {
            std::snprintf(headline, sizeof(headline), "ALARM %ld", static_cast<long>(_last_alarm_code));
        } else {
            std::snprintf(headline, sizeof(headline), "ALARM");
        }

        const char* detail = "TOUCH UNLOCK TO CLEAR";
        if (_alarm_clear == AlarmClear::ResetSent) {
            detail = "RESET SENT; $X FOLLOWS";
        } else {
            const char* hint = alarmHint(_last_alarm_code);
            if (hint[0] != '\0') {
                detail = hint;
            }
        }

        std::string signature(headline);
        signature += '|';
        signature += detail;
        if (_alarm_banner_shown && _rendered_alarm == signature) {
            return true;
        }

        if (!_alarm_banner_shown) {
            _display.fillRect(0, AlarmTop, ScreenWidth, AlarmHeight, Red);
            _display.drawRect(0, AlarmTop, ScreenWidth, AlarmHeight, White);
            drawButton(AlarmButtonX, AlarmButtonY, AlarmButtonW, AlarmButtonH, "UNLOCK", true);
            _alarm_banner_shown = true;
        }
        _rendered_alarm = signature;
        _display.fillRect(8, AlarmTop + 8, AlarmButtonX - 20, 60, Red);
        _display.drawText(12, AlarmTop + 10, headline, White, Red, 2);
        _display.drawText(12, AlarmTop + 40, detail, Yellow, Red, 2);
        return true;
    }

    void TS35Module::serviceAlarmClear(uint32_t now) {
        if (_alarm_clear != AlarmClear::ResetSent) {
            return;
        }
        if (static_cast<int32_t>(_alarm_clear_at_ms - now) > 0) {
            return;
        }
        _alarm_clear = AlarmClear::None;
        if (_model.snapshot().state != MachineState::Alarm) {
            return;  // the reset alone already cleared it; nothing left to unlock
        }
        push(std::string_view("$X\n"));
        _last_command_ms = now;
    }

    bool TS35Module::handleAlarmBannerTap(int16_t x, int16_t y, uint32_t now) {
        if (!_alarm_banner_shown) {
            return false;
        }
        if (y < AlarmTop || y >= AlarmTop + AlarmHeight) {
            return false;
        }
        if (!hit(x, y, AlarmButtonX, AlarmButtonY, AlarmButtonW, AlarmButtonH)) {
            return true;  // a touch inside the banner must not leak to the page under it
        }
        if (_alarm_clear != AlarmClear::None || now - _last_realtime_ms < RealtimeCooldownMs) {
            return true;
        }
        // Soft reset now, $X after AlarmClearGapMs. Neither one moves the machine,
        // so the sequence does not depend on _controls_enabled.
        push(TS35Model::realtimeByte(RealtimeAction::EmergencyReset));
        _last_realtime_ms  = now;
        _alarm_clear       = AlarmClear::ResetSent;
        _alarm_clear_at_ms = now + AlarmClearGapMs;
        return true;
    }

    void TS35Module::renderNotice(uint32_t now) {
        if (!_notice.empty() && static_cast<int32_t>(_notice_expires_ms - now) <= 0) {
            _notice.clear();
            if (_confirm_action != ConfirmAction::None && static_cast<int32_t>(_confirm_expires_ms - now) <= 0) {
                clearConfirmation();
            }
        }
        const bool visible = !_notice.empty();
        drawCachedText(_rendered_notice, visible ? _notice.c_str() : "", 78, 6, NoticeY, visible ? Yellow : Black, visible ? Navy : Black, 1);
    }

    void TS35Module::drawCachedText(std::string& rendered,
                                    const char*  text,
                                    size_t       characters,
                                    int16_t      x,
                                    int16_t      y,
                                    uint16_t     foreground,
                                    uint16_t     background,
                                    uint8_t      scale) {
        std::string field(text == nullptr ? "" : text);
        if (field.size() > characters) {
            field.resize(characters);
        } else if (field.size() < characters) {
            field.append(characters - field.size(), ' ');
        }
        if (rendered == field) {
            return;
        }
        rendered = field;
        _display.drawText(x, y, rendered.c_str(), foreground, background, scale);
    }

    void TS35Module::resetPageRenderCache() {
        for (std::string& axis : _rendered_axes) {
            axis.clear();
        }
        _rendered_footer.clear();
        _rendered_step.clear();
        _rendered_step_z.clear();
        _rendered_speed.clear();
        _rendered_zero_hint.clear();
        _rendered_overrides.clear();
        for (std::string& line : _rendered_info) {
            line.clear();
        }
    }

    void TS35Module::drawButton(int16_t x, int16_t y, int16_t width, int16_t height, const char* label, bool enabled, bool selected) {
        const uint16_t fill = selected ? Blue : (enabled ? DarkGray : Black);
        const uint16_t edge = selected ? Cyan : (enabled ? LightGray : DarkGray);
        const uint16_t text = enabled ? White : LightGray;
        _display.fillRect(x + 1, y + 1, width - 2, height - 2, fill);
        _display.drawRect(x, y, width, height, edge);
        const int16_t textWidth = static_cast<int16_t>(std::strlen(label) * 12);
        const int16_t textX     = std::max<int16_t>(x + 5, x + (width - textWidth) / 2);
        const int16_t textY     = y + (height - 14) / 2;
        _display.drawText(textX, textY, label, text, fill, 2);
    }

    void TS35Module::setNotice(const char* text, uint32_t now, uint32_t durationMs) {
        _notice            = text;
        _notice_expires_ms = now + durationMs;
    }

    void TS35Module::clearConfirmation() {
        _confirm_action     = ConfirmAction::None;
        _confirm_expires_ms = 0;
    }

    bool TS35Module::hit(int16_t x, int16_t y, int16_t left, int16_t top, int16_t width, int16_t height) {
        return x >= left && x < left + width && y >= top && y < top + height;
    }

    uint16_t TS35Module::stateColor(MachineState state) {
        switch (state) {
            case MachineState::Idle:
                return DarkGreen;
            case MachineState::Cycle:
            case MachineState::Jog:
            case MachineState::Homing:
                return Blue;
            case MachineState::Hold:
            case MachineState::SafetyDoor:
                return Orange;
            case MachineState::Alarm:
            case MachineState::ConfigAlarm:
            case MachineState::Critical:
                return Red;
            default:
                return DarkGray;
        }
    }

    const char* TS35Module::pageName(Page page) {
        switch (page) {
            case Page::Dro:
                return "DRO";
            case Page::Jog:
                return "JOG";
            case Page::Zero:
                return "ZERO";
            case Page::Control:
                return "CTRL";
            case Page::Info:
                return "INFO";
        }
        return "?";
    }

    const char* TS35Module::unitsName(Units units) {
        switch (units) {
            case Units::Millimeters:
                return "MM";
            case Units::Inches:
                return "IN";
            default:
                return "?";
        }
    }

    namespace {
        ConfigurableModuleFactory::InstanceBuilder<TS35Module> registration("ts35");
    }

}  // namespace ts35
