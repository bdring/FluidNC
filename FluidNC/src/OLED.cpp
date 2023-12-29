#include "OLED.h"

#include "Machine/MachineConfig.h"

void OLED::show(Layout& layout, const char* msg) {
    if (_width < layout._width_required) {
        return;
    }
    _oled->setTextAlignment(layout._align);
    _oled->setFont(layout._font);
    _oled->drawString(layout._x, layout._y, msg);
}

OLED::Layout OLED::bannerLayout128  = { 0, 0, 0, ArialMT_Plain_24, TEXT_ALIGN_CENTER };
OLED::Layout OLED::bannerLayout64   = { 0, 0, 0, ArialMT_Plain_16, TEXT_ALIGN_CENTER };
OLED::Layout OLED::stateLayout      = { 0, 0, 0, ArialMT_Plain_16, TEXT_ALIGN_LEFT };
OLED::Layout OLED::tickerLayout     = { 63, 0, 128, ArialMT_Plain_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::filenameLayout   = { 63, 13, 128, ArialMT_Plain_10, TEXT_ALIGN_CENTER };
OLED::Layout OLED::percentLayout128 = { 128, 0, 128, ArialMT_Plain_16, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::percentLayout64  = { 64, 0, 64, ArialMT_Plain_16, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::limitLabelLayout = { 80, 14, 128, ArialMT_Plain_10, TEXT_ALIGN_LEFT };
OLED::Layout OLED::posLabelLayout   = { 60, 14, 128, ArialMT_Plain_10, TEXT_ALIGN_RIGHT };
OLED::Layout OLED::radioAddrLayout  = { 50, 0, 128, ArialMT_Plain_10, TEXT_ALIGN_LEFT };

void OLED::afterParse() {
    if (!config->_i2c[_i2c_num]) {
        log_error("i2c" << _i2c_num << " section must be defined for OLED");
        _error = true;
        return;
    }
    switch (_width) {
        case 128:
            switch (_height) {
                case 64:
                    _geometry = GEOMETRY_128_64;
                    break;
                case 32:
                    _geometry = GEOMETRY_128_32;
                    break;
                default:
                    log_error("For OLED width 128, height must be 32 or 64");
                    _error = true;
                    break;
            }
            break;
        case 64:
            switch (_height) {
                case 48:
                    _geometry = GEOMETRY_64_48;
                    break;
                case 32:
                    _geometry = GEOMETRY_64_32;
                    break;
                default:
                    log_error("For OLED width 64, height must be 32 or 48");
                    _error = true;
                    break;
            }
            break;
        default:
            log_error("OLED width must be 64 or 128");
            _error = true;
    }
}

void OLED::init() {
    if (_error) {
        return;
    }
    log_info("OLED I2C address: " << to_hex(_address) << " width: " << _width << " height: " << _height);
    _oled = new SSD1306_I2C(_address, _geometry, config->_i2c[_i2c_num], 400000);
    _oled->init();

    _oled->flipScreenVertically();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    _oled->clear();

    show((_width == 128) ? bannerLayout128 : bannerLayout64, "FluidNC");

    _oled->display();

    allChannels.registration(this);
    setReportInterval(_report_interval_ms);
}

Channel* OLED::pollLine(char* line) {
    autoReport();
    return nullptr;
}

void OLED::show_state() {
    show(stateLayout, _state);
}

void OLED::show_limits(bool probe, const bool* limits) {
    if (_width != 128) {
        return;
    }
    if (_filename.length() != 0) {
        return;
    }
    if (_state == "Alarm") {
        return;
    }
    for (uint8_t axis = X_AXIS; axis < 3; axis++) {
        draw_checkbox(80, 27 + (axis * 10), 7, 7, limits[axis]);
    }
}
void OLED::show_file() {
    int pct = int(_percent);
    if (_filename.length() == 0) {
        return;
    }
    if (_state != "Run" && pct == 100) {
        // This handles the case where the system returns to idle
        // but shows one last SD report
        return;
    }
    if (_width == 128) {
        show(percentLayout128, std::to_string(pct) + '%');

        _ticker += "-";
        if (_ticker.length() >= 12) {
            _ticker = "-";
        }
        show(tickerLayout, _ticker);

        wrapped_draw_string(14, _filename, ArialMT_Plain_16);

        _oled->drawProgressBar(0, 45, 120, 10, pct);
    } else {
        show(percentLayout64, std::to_string(pct) + '%');
    }
}
void OLED::show_dro(const float* axes, bool isMpos, bool* limits) {
    if (_state == "Alarm") {
        return;
    }
    if (_width == 128 && _filename.length()) {
        // wide displays will show a progress bar instead of DROs
        return;
    }

    auto n_axis = config->_axes->_numberAxis;
    char axisVal[20];

    show(limitLabelLayout, "L");
    show(posLabelLayout, isMpos ? "M Pos" : "W Pos");

    _oled->setFont(ArialMT_Plain_10);
    uint8_t oled_y_pos;
    for (uint8_t axis = X_AXIS; axis < n_axis; axis++) {
        oled_y_pos = ((_height == 64) ? 24 : 17) + (axis * 10);

        std::string axis_msg(1, Machine::Axes::_names[axis]);
        if (_width == 128) {
            axis_msg += ":";
        } else {
            // For small displays there isn't room for separate limit boxes
            // so we put it after the label
            axis_msg += limits[axis] ? "L" : ":";
        }
        _oled->setTextAlignment(TEXT_ALIGN_LEFT);
        _oled->drawString(0, oled_y_pos, axis_msg.c_str());

        _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
        snprintf(axisVal, 20 - 1, "%.3f", axes[axis]);
        _oled->drawString((_width == 128) ? 60 : 63, oled_y_pos, axisVal);
    }
    _oled->display();
}

void OLED::show_radio_info() {
    if (_filename.length()) {
        return;
    }
    if (_width == 128) {
        if (_state == "Alarm") {
            wrapped_draw_string(18, _radio_info, ArialMT_Plain_10);
            wrapped_draw_string(30, _radio_addr, ArialMT_Plain_10);
        } else if (_state != "Run") {
            show(radioAddrLayout, _radio_addr);
        }
    } else {
        if (_state == "Alarm") {
            wrapped_draw_string(10, _radio_info, ArialMT_Plain_10);
            wrapped_draw_string(28, _radio_addr, ArialMT_Plain_10);
        }
    }
}

void OLED::parse_numbers(std::string s, float* nums, int maxnums) {
    size_t pos     = 0;
    size_t nextpos = -1;
    size_t i;
    do {
        if (i >= maxnums) {
            return;
        }
        nextpos   = s.find_first_of(",", pos);
        auto num  = s.substr(pos, nextpos - pos);
        nums[i++] = std::strtof(num.c_str(), nullptr);
        pos       = nextpos + 1;
    } while (nextpos != std::string::npos);
}

void OLED::parse_axes(std::string s, float* axes) {
    size_t pos     = 0;
    size_t nextpos = -1;
    size_t axis    = 0;
    do {
        nextpos  = s.find_first_of(",", pos);
        auto num = s.substr(pos, nextpos - pos);
        if (axis < MAX_N_AXIS) {
            axes[axis++] = std::strtof(num.c_str(), nullptr);
        }
        pos = nextpos + 1;
    } while (nextpos != std::string::npos);
}

void OLED::parse_status_report() {
    if (_report.back() == '>') {
        _report.pop_back();
    }
    // Now the string is a sequence of field|field|field
    size_t pos     = 0;
    auto   nextpos = _report.find_first_of("|", pos);
    _state         = _report.substr(pos + 1, nextpos - pos - 1);

    bool probe              = false;
    bool limits[MAX_N_AXIS] = { false };

    float axes[MAX_N_AXIS];
    bool  isMpos = false;
    _filename    = "";

    // ... handle it
    while (nextpos != std::string::npos) {
        pos        = nextpos + 1;
        nextpos    = _report.find_first_of("|", pos);
        auto field = _report.substr(pos, nextpos - pos);
        // MPos:, WPos:, Bf:, Ln:, FS:, Pn:, WCO:, Ov:, A:, SD: (ISRs:, Heap:)
        auto colon = field.find_first_of(":");
        auto tag   = field.substr(0, colon);
        auto value = field.substr(colon + 1);
        if (tag == "MPos") {
            // x,y,z,...
            parse_axes(value, axes);
            isMpos = true;
            continue;
        }
        if (tag == "WPos") {
            // x,y,z...
            parse_axes(value, axes);
            isMpos = false;
            continue;
        }
        if (tag == "Bf") {
            // buf_avail,rx_avail
            continue;
        }
        if (tag == "Ln") {
            // n
            auto linenum = std::strtol(value.c_str(), nullptr, 10);
            continue;
        }
        if (tag == "FS") {
            // feedrate,spindle_speed
            float fs[2];
            parse_numbers(value, fs, 2);  // feed in [0], spindle in [1]
            continue;
        }
        if (tag == "Pn") {
            // PXxYy etc
            for (char const& c : value) {
                switch (c) {
                    case 'P':
                        probe = true;
                        break;
                    case 'X':
                        limits[X_AXIS] = true;
                        break;
                    case 'Y':
                        limits[Y_AXIS] = true;
                        break;
                    case 'Z':
                        limits[Z_AXIS] = true;
                        break;
                    case 'A':
                        limits[A_AXIS] = true;
                        break;
                    case 'B':
                        limits[B_AXIS] = true;
                        break;
                    case 'C':
                        limits[C_AXIS] = true;
                        break;
                }
                continue;
            }
        }
        if (tag == "WCO") {
            // x,y,z,...
            // We do not use the WCO values because the DROs show whichever
            // position is in the status report
            // float wcos[MAX_N_AXIS];
            // auto  wcos = parse_axes(value, wcos);
            continue;
        }
        if (tag == "Ov") {
            // feed_ovr,rapid_ovr,spindle_ovr
            float frs[3];
            parse_numbers(value, frs, 3);  // feed in [0], rapid in [1], spindle in [2]
            continue;
        }
        if (tag == "A") {
            // SCFM
            int  spindle = 0;
            bool flood   = false;
            bool mist    = false;
            for (char const& c : value) {
                switch (c) {
                    case 'S':
                        spindle = 1;
                        break;
                    case 'C':
                        spindle = 2;
                        break;
                    case 'F':
                        flood = true;
                        break;
                    case 'M':
                        mist = true;
                        break;
                }
            }
            continue;
        }
        if (tag == "SD") {
            auto commaPos = value.find_first_of(",");
            _percent      = std::strtof(value.substr(0, commaPos).c_str(), nullptr);
            _filename     = value.substr(commaPos + 1);
            continue;
        }
    }
    _oled->clear();
    show_state();
    show_file();
    show_limits(probe, limits);
    show_dro(axes, isMpos, limits);
    show_radio_info();
    _oled->display();
}

void OLED::parse_gcode_report() {
    size_t pos     = 0;
    size_t nextpos = _report.find_first_of(":", pos);
    auto   name    = _report.substr(pos, nextpos - pos);
    if (name != "[GC") {
        return;
    }
    pos = nextpos + 1;
    do {
        nextpos  = _report.find_first_of(" ", pos);
        auto tag = _report.substr(pos, nextpos - pos);
        // G80 G0 G1 G2 G3  G38.2 G38.3 G38.4 G38.5
        // G54 .. G59
        // G17 G18 G19
        // G20 G21
        // G90 G91
        // G94 G93
        // M0 M1 M2 M30
        // M3 M4 M5
        // M7 M8 M9
        // M56
        // Tn
        // Fn
        // Sn
        //        if (tag == "G0") {
        //            continue;
        //        }
        pos = nextpos + 1;
    } while (nextpos != std::string::npos);
}

// [MSG:INFO: Connecting to STA:SSID foo]
void OLED::parse_STA() {
    size_t start = strlen("[MSG:INFO: Connecting to STA SSID:");
    _radio_info  = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    _oled->display();
}

// [MSG:INFO: Connected - IP is 192.168.68.134]
void OLED::parse_IP() {
    size_t start = _report.rfind(" ") + 1;
    _radio_addr  = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    wrapped_draw_string(fh * 2, _radio_addr, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

// [MSG:INFO: AP SSID foo IP 192.168.68.134 mask foo channel foo]
void OLED::parse_AP() {
    size_t start    = strlen("[MSG:INFO: AP SSID ");
    size_t ssid_end = _report.rfind(" IP ");
    size_t ip_end   = _report.rfind(" mask ");
    size_t ip_start = ssid_end + strlen(" IP ");

    _radio_info = "AP: ";
    _radio_info += _report.substr(start, ssid_end - start);
    _radio_addr = _report.substr(ip_start, ip_end - ip_start);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    wrapped_draw_string(fh * 2, _radio_addr, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

void OLED::parse_BT() {
    size_t      start  = strlen("[MSG:INFO: BT Started with ");
    std::string btname = _report.substr(start, _report.size() - start - 1);
    _radio_info        = "BT: ";
    _radio_info += btname.c_str();

    _oled->clear();
    wrapped_draw_string(0, _radio_info, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

void OLED::parse_WebUI() {
    size_t      start  = strlen("[MSG:INFO: WebUI: Request from ");
    std::string ipaddr = _report.substr(start, _report.size() - start - 1);

    _oled->clear();
    auto fh = font_height(ArialMT_Plain_10);
    wrapped_draw_string(0, "WebUI from", ArialMT_Plain_10);
    wrapped_draw_string(fh * 2, ipaddr, ArialMT_Plain_10);
    _oled->display();
    delay_ms(_radio_delay);
}

void OLED::parse_report() {
    if (_report.length() == 0) {
        return;
    }
    if (_report.rfind("<", 0) == 0) {
        parse_status_report();
        return;
    }
    if (_report.rfind("[GC:", 0) == 0) {
        parse_gcode_report();
        return;
    }
    if (_report.rfind("[MSG:INFO: Connecting to STA SSID:", 0) == 0) {
        parse_STA();
        return;
    }
    if (_report.rfind("[MSG:INFO: Connected", 0) == 0) {
        parse_IP();
        return;
    }
    if (_report.rfind("[MSG:INFO: AP SSID ", 0) == 0) {
        parse_AP();
        return;
    }
    if (_report.rfind("[MSG:INFO: BT Started with ", 0) == 0) {
        parse_BT();
        return;
    }
    if (_report.rfind("[MSG:INFO: WebUI: Request from ", 0) == 0) {
        parse_WebUI();
        return;
    }
}

// This is how the OLED driver receives channel data
size_t OLED::write(uint8_t data) {
    char c = data;
    if (c == '\r') {
        return 1;
    }
    if (c == '\n') {
        parse_report();
        _report = "";
        return 1;
    }
    _report += c;
    return 1;
}

uint8_t OLED::font_width(font_t font) {
    return ((uint8_t*)font)[0];
}
uint8_t OLED::font_height(font_t font) {
    return ((uint8_t*)font)[1];
}
struct glyph_t {
    uint8_t msb;
    uint8_t lsb;
    uint8_t size;
    uint8_t width;
};
struct xfont_t {
    uint8_t width;
    uint8_t height;
    uint8_t first;
    uint8_t nchars;
    glyph_t glyphs[];
};
size_t OLED::char_width(char c, font_t font) {
    xfont_t* xf    = (xfont_t*)font;
    int      index = c - xf->first;
    return (index < 0) ? 0 : xf->glyphs[index].width;
}

void OLED::wrapped_draw_string(int16_t y, const std::string& s, font_t font) {
    _oled->setFont(font);
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    size_t slen   = s.length();
    size_t swidth = 0;
    size_t i;
    for (i = 0; i < slen && swidth < _width; i++) {
        swidth += char_width(s[i], font);
        if (swidth > _width) {
            break;
        }
    }
    if (swidth < _width) {
        _oled->drawString(0, y, s.c_str());
    } else {
        _oled->drawString(0, y, s.substr(0, i).c_str());
        _oled->drawString(0, y + font_height(font) - 1, s.substr(i, slen).c_str());
    }
}

void OLED::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        _oled->fillRect(x, y, width, height);  // If log.0
    } else {
        _oled->drawRect(x, y, width, height);  // If log.1
    }
}
