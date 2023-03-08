#pragma once

#include <Arduino.h>
#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;

class FluidPath : public stdfs::path {
public:
    FluidPath(const char* name, const char* fs, std::error_code& ec) noexcept : FluidPath(name, fs, &ec) {}
    FluidPath(String name, const char* fs, std::error_code& ec) noexcept : FluidPath(name.c_str(), fs, &ec) {}
    //    FluidPath(std::string name, std::error_code& ec) noexcept : FluidPath(name.c_str(), &ec) {}
    FluidPath(const char* name, const char* fs) : FluidPath(name, fs, nullptr) {}
    FluidPath(String name, const char* fs) : FluidPath(name.c_str(), fs) {}
    //    FluidPath(std::string name) : FluidPath(name.c_str()) {}

    ~FluidPath();

    FluidPath() = default;

    FluidPath(const FluidPath& o);             // copy
    FluidPath(FluidPath&& o);                  // move
    FluidPath& operator=(const FluidPath& o);  // copy assignment
    FluidPath& operator=(FluidPath&& o);       // move assignment

    // true if there is something after the mount name.
    // /localfs/foo -> true,  /localfs -> false
    bool hasTail() { return ++(++begin()) != end(); }

private:
    FluidPath(const char* name, const char* fs, std::error_code*);

    static int _refcnt;
    bool       _isSD = false;
};
