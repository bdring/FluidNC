#pragma once

class Macro {
    std::string _name;

public:
    std::string        _gcode;
    bool               run();
    void               set(const char* value) { _gcode = value; }
    void               set(const std::string value) { _gcode = value; }
    void               set(const std::string_view value) { _gcode = value; }
    const std::string& get() { return _gcode; }
    const char*        name() { return _name.c_str(); }
    Macro(std::string name) : _name(name) {}
    Macro() = default;
};