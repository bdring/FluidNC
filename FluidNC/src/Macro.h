#pragma once

class Macro {
public:
    std::string _name;
    std::string _value;
    Macro(std::string name) : _name(name) {}
    Macro() = default;
};
