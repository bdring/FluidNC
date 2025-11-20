#pragma once
#include "Channel.h"
#include <cstdarg>

class Macro {
    std::string _name;

public:
    std::string        _gcode;
    bool               run(Channel* channel);
    void               set(const char* value) { _gcode = value; }
    void               set(const std::string& value) { _gcode = value; }
    void               set(const std::string_view value) { _gcode = value; }
    void               erase() { _gcode = ""; }
    const std::string& get() { return _gcode; }
    const char*        name() { return _name.c_str(); }

    // add to _gcode using a printf style formatting like _macro.addf("G53G0Z%0.3f", _safe_z);
    void addf(const char* format, ...) {
        char    loc_buf[100];
        char*   temp = loc_buf;
        va_list arg;
        va_list copy;
        va_start(arg, format);
        va_copy(copy, arg);
        size_t len = vsnprintf(NULL, 0, format, arg);
        va_end(copy);

        if (len >= sizeof(loc_buf)) {
            temp = new char[len + 1];
            if (temp == NULL) {
                return;
            }
        }
        len = vsnprintf(temp, len + 1, format, arg);

        if (!_gcode.empty()) {  // if we are we adding to existing, we need an '&'
            _gcode += "&";
        }

        _gcode += std::string(temp);
    }

    explicit Macro(const std::string& name) : _name(name) {}
    Macro() = default;
};
