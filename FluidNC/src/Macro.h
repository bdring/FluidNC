#pragma once
#include "Channel.h"

class Macro {
    std::string _name;

public:
    std::string        _gcode;
    bool               run(Channel* channel);
    void               set(const char* value) { _gcode = value; }
    void               set(const std::string& value) { _gcode = value; }
    void               set(const std::string_view value) { _gcode = value; }
    const std::string& get() { return _gcode; }
    const char*        name() { return _name.c_str(); }
    explicit Macro(const std::string& name) : _name(name) {}
    Macro() = default;
};
