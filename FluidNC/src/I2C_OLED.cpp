#include "I2C_OLED.h"
#include "Machine/MachineConfig.h"  // config

void I2C_OLED::afterParse() {
    if (_sda_pin.undefined()) {
        log_error("OLED sda_pin must be configured");
        _error = true;
    }
    if (_scl_pin.undefined()) {
        log_error("OLED scl_pin must be configured");
        _error = true;
    }
    if (!_sda_pin.capabilities().has(Pin::Capabilities::Input | Pin::Capabilities::Output | Pin::Capabilities::Native)) {
        log_error("OLED sda_pin must be a native gpio with input and output capability");
        _error = true;
    }
    if (!_scl_pin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
        log_error("OLED scl_pin must be a native gpio with input capability");
        _error = true;
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
            log_error("I2C_OLED width must be 64 or 128");
            _error = true;
    }
}

void I2C_OLED::init() {
    if (_error) {
        return;
    }
    log_info("OLED I2C SDA:" << _sda_pin.name() << " SCL:" << _scl_pin.name() << " address:" << to_hex(_address) << " width: " << _width
                             << " height: " << _height);
    auto sdaPin = _sda_pin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Output);
    auto sclPin = _scl_pin.getNative(Pin::Capabilities::Output);
    _oled       = new SSD1306Wire(_address, sdaPin, sclPin, _geometry, I2C_ONE, 400000);
    _oled->init();

    _oled->flipScreenVertically();
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);

    _oled->clear();

    _oled->setFont(ArialMT_Plain_24);
    _oled->drawString(10, 20, "FluidNC");

    _oled->display();

    allChannels.registration(this);
    setReportInterval(500);
    delay(1000);
}

Channel* I2C_OLED::pollLine(char* line) {
    autoReport();
    return nullptr;
}

void I2C_OLED::show_state(std::string& state) {
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(ArialMT_Plain_16);
    _oled->drawString(0, 0, (String)state.c_str());
}

void I2C_OLED::show_limits(bool probe, const bool* limits) {
    for (uint8_t axis = X_AXIS; axis < 3; axis++) {
        draw_checkbox(80, 27 + (axis * 10), 7, 7, limits[axis]);
    }
}
void I2C_OLED::show_file(float percent, const char* filename) {
    _oled->setTextAlignment(TEXT_ALIGN_CENTER);
    _oled->setFont(ArialMT_Plain_10);
    std::string state_string = "File";

    int file_ticker = 0;
    for (int i = 0; i < file_ticker % 10; i++) {
        state_string += ".";
    }
    file_ticker++;
    _oled->drawString(63, 0, state_string.c_str());

    _oled->drawString(63, 12, filename);

    int progress = percent;

    // draw the progress bar
    _oled->drawProgressBar(0, 45, 120, 10, progress);

    _oled->setFont(ArialMT_Plain_10);
    _oled->setTextAlignment(TEXT_ALIGN_CENTER);
    _oled->drawString(64, 25, String(progress) + "%");
    _oled->display();
}

void I2C_OLED::show_dro(const float* axes, bool is_mpos) {
    _oled->setTextAlignment(TEXT_ALIGN_LEFT);
    _oled->setFont(ArialMT_Plain_10);

    char axisVal[20];

    _oled->drawString(80, 14, "L");  // Limit switch

    auto n_axis = config->_axes->_numberAxis;

    _oled->setTextAlignment(TEXT_ALIGN_RIGHT);

    _oled->drawString(60, 14, is_mpos ? "M Pos" : "W Pos");

    uint8_t oled_y_pos;
    for (uint8_t axis = X_AXIS; axis < n_axis; axis++) {
        oled_y_pos = 24 + (axis * 10);

        String axis_letter = String(Machine::Axes::_names[axis]);
        axis_letter += ":";
        _oled->setTextAlignment(TEXT_ALIGN_LEFT);
        _oled->drawString(0, oled_y_pos, axis_letter);

        _oled->setTextAlignment(TEXT_ALIGN_RIGHT);
        snprintf(axisVal, 20 - 1, "%.3f", axes[axis]);
        _oled->drawString(60, oled_y_pos, axisVal);

        //if (bitnum_is_true(limitAxes, axis)) {  // only draw the box if a switch has been defined
        //    draw_checkbox(80, 27 + (axis * 10), 7, 7, limits_check(bitnum_to_mask(axis)));
        //}
    }
    _oled->display();
}

void I2C_OLED::parse_numbers(std::string s, float* nums, int maxnums) {
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

float* I2C_OLED::parse_axes(std::string s) {
    static float axes[MAX_N_AXIS];
    size_t       pos     = 0;
    size_t       nextpos = -1;
    size_t       axis    = 0;
    do {
        nextpos  = s.find_first_of(",", pos);
        auto num = s.substr(pos, nextpos - pos);
        if (axis < MAX_N_AXIS) {
            axes[axis++] = std::strtof(num.c_str(), nullptr);
        }
        pos = nextpos + 1;
    } while (nextpos != std::string::npos);
    return axes;
}

void I2C_OLED::parse_status_report() {
    if (_report.back() == '>') {
        _report.pop_back();
    }
    // Now the string is a sequence of field|field|field
    size_t pos     = 0;
    auto   nextpos = _report.find_first_of("|", pos);
    auto   state   = _report.substr(pos + 1, nextpos - pos - 1);

    bool probe              = false;
    bool limits[MAX_N_AXIS] = { false };

    _oled->clear();

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
            auto mpos = parse_axes(value);
            show_dro(mpos, true);
            continue;
        }
        if (tag == "WPos") {
            // x,y,z...
            auto wpos = parse_axes(value);
            show_dro(wpos, false);
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
        show_limits(probe, limits);
        if (tag == "WCO") {
            // x,y,z,...
            auto wcos = parse_axes(value);
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
            auto comma   = value.find_first_of(",");
            auto percent = std::strtof(value.substr(0, comma).c_str(), nullptr);
            auto file    = value.substr(comma + 1);
            show_file(percent, file.c_str());
            continue;
        }
    }
    show_state(state);
    _oled->display();
}

void I2C_OLED::parse_gcode_report() {
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

void I2C_OLED::parse_report() {
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
}

size_t I2C_OLED::write(uint8_t data) {
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

void I2C_OLED::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        _oled->fillRect(x, y, width, height);  // If log.0
    } else {
        _oled->drawRect(x, y, width, height);  // If log.1
    }
}
