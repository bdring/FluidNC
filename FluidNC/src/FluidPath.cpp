// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FluidPath.h"
#include "Driver/sdspi.h"
#include "Config.h"
#include "Error.h"
#include "Machine/MachineConfig.h"
#include "FluidError.hpp"
#include "HashFS.h"

uint32_t FluidPath::_refcnt = 0;

FluidPath::FluidPath(const char* name, const char* fs, std::error_code* ecptr) : std::filesystem::path(canonicalPath(name, fs)) {
    auto mount = *(++begin());  // Use the path iterator to get the first component
    _isSD      = mount == "sd";

    if (_isSD) {
        if (!config->_sdCard->config_ok) {
            std::error_code ec = FluidError::SDNotConfigured;
            if (ecptr) {
                *ecptr = ec;
                return;
            }
            log_info("SD card is inaccessible");
            throw Error::FsFailedMount;
        }
        if (_refcnt == 0) {
            auto ec = sd_mount();
            if (ec) {
                if (ecptr) {
                    *ecptr = ec;
                    return;
                }
                log_info("SD card is inaccessible");
                throw Error::FsFailedMount;
            }
        }
        ++_refcnt;
    }
}

FluidPath::FluidPath(const FluidPath& o) : path(o), _isSD(o._isSD) {
    if (this != &o && _isSD) {
        ++_refcnt;
    }
}

FluidPath::FluidPath(FluidPath&& o) : path(std::move(o)), _isSD(o._isSD) {
    if (this != &o) {
        // After a move, the other object is dead so we do not want
        // to decrement the refcount on destruction
        o._isSD = false;
    }
}

FluidPath& FluidPath::operator=(const FluidPath& o) {
    stdfs::path::operator=(o);

    _isSD = o._isSD;
    if (&o != this && _isSD) {
        ++_refcnt;
    }
    return *this;
}

FluidPath& FluidPath::operator=(FluidPath&& o) {
    std::swap(_isSD, o._isSD);
    stdfs::path::operator=(std::move(o));
    return *this;
}

FluidPath::~FluidPath() {
    if (_isSD && (_refcnt && --_refcnt == 0)) {
        sd_unmount();
    }
}
