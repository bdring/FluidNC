#pragma once

#include <filesystem>
#include <string_view>
#include <memory>
#include "Driver/localfs.h"

namespace stdfs = std::filesystem;

struct Volume {
    const char* name;
    std::string prefix;
};
extern Volume SD;
extern Volume LocalFS;

// Helper class to manage SD card mount/unmount lifecycle
class SDMountState {
public:
    SDMountState();
    ~SDMountState();
};

class FluidPath : public stdfs::path {
private:
    FluidPath(const std::string_view name, const Volume& fs, std::error_code*);

    std::shared_ptr<SDMountState> _sd_mount_state;
    bool                          _isSD = false;

public:
    FluidPath(const std::string_view name, const Volume& fs, std::error_code& ec) noexcept : FluidPath(name, fs, &ec) {}
    FluidPath(const std::string_view name, const Volume& fs) : FluidPath(name, fs, nullptr) {}

    ~FluidPath() = default;

    FluidPath() = default;

    FluidPath(const FluidPath& o) = default;             // copy
    FluidPath(FluidPath&& o) = default;                  // move
    FluidPath& operator=(const FluidPath& o) = default;  // copy assignment
    FluidPath& operator=(FluidPath&& o) = default;       // move assignment

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
