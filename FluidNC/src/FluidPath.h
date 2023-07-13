#pragma once

#include <Arduino.h>
#include <filesystem>
#include <string>
#include "Driver/localfs.h"

namespace stdfs = std::filesystem;

class FluidPath : public stdfs::path {
public:
    FluidPath(const char* name, const char* fs, std::error_code& ec) noexcept : FluidPath(name, fs, &ec) {}
    FluidPath(const std::string& name, const char* fs, std::error_code& ec) noexcept : FluidPath(name.c_str(), fs, &ec) {}
    FluidPath(const char* name, const char* fs) : FluidPath(name, fs, nullptr) {}
    FluidPath(const std::string& name, const char* fs) : FluidPath(name.c_str(), fs) {}

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
