#pragma once

#include <filesystem>
#include <string_view>
#include "Driver/localfs.h"

namespace stdfs = std::filesystem;

struct Volume {
    const char* name;
    std::string prefix;
};
extern Volume SD;
extern Volume LocalFS;

class FluidPath : public stdfs::path {
private:
    FluidPath(const std::string_view name, const Volume& fs, std::error_code*);

    static uint32_t _refcnt;
    bool            _isSD = false;

public:
    FluidPath(const std::string_view name, const Volume& fs, std::error_code& ec) noexcept : FluidPath(name, fs, &ec) {}
    FluidPath(const std::string_view name, const Volume& fs) : FluidPath(name, fs, nullptr) {}

    ~FluidPath();

    FluidPath() = default;

    FluidPath(const FluidPath& o);             // copy
    FluidPath(FluidPath&& o);                  // move
    FluidPath& operator=(const FluidPath& o);  // copy assignment
    FluidPath& operator=(FluidPath&& o);       // move assignment

    // true if there is something after the mount name.
    // /localfs/foo -> true,  /localfs -> false
    bool hasTail() { return ++(++begin()) != end(); }

    static const std::string canonPath(const std::string_view filename, const Volume& defaultFs);
};

#include <Print.h>
inline Print& operator<<(Print& lhs, FluidPath path) {
    lhs.print(path.string().c_str());
    return lhs;
}
