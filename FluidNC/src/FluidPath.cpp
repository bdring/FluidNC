// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FluidPath.h"
#include "Driver/sdspi.h"
#include "Config.h"
#include "Error.h"
#include "Machine/MachineConfig.h"
#include "FluidError.hpp"
#include "HashFS.h"
#include "string_util.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mutex>

Volume SD { "sd" };
Volume LocalFS { "localfs" };

uint32_t FluidPath::_refcnt = 0;
static std::mutex sd_refcnt_mutex;
static SemaphoreHandle_t sd_mount_lock = nullptr;

static void ensure_sd_mount_lock() {
    if (!sd_mount_lock) {
        sd_mount_lock = xSemaphoreCreateBinary();
        if (sd_mount_lock) {
            xSemaphoreGive(sd_mount_lock);
        }
    }
}

const std::string FluidPath::canonPath(std::string_view filename, const Volume& defaultFs) {
    std::string ret;
    //    log_debug("fn " << filename << " fs " << defaultFs.name);
    if (filename.empty()) {
        ret = defaultFs.prefix;
        return ret;
    }

    // A std::filesystem::path with a trailing slash (except for just "/")
    // is considered to be a path with an empty final component, not a
    // final directory component.  That causes problems when trying to
    // determine the file type, so we remove trailing slases.
    while (filename.length() > 1 && filename[filename.length() - 1] == '/') {
        filename.remove_suffix(1);
    }

    if (filename[0] == '/') {
        auto        pos = filename.find('/', 1);
        std::string fsname;
        std::string tail;
        if (pos != std::string::npos) {
            fsname = filename.substr(1, pos - 1);
            tail   = filename.substr(pos);
        } else {
            fsname = filename.substr(1);
            tail   = "";
        }
        //            log_debug("FS " << fsname << " tail " << tail << " fn " << filename);
        if (string_util::equal_ignore_case(fsname, LocalFS.name) || string_util::equal_ignore_case(fsname, "spiffs") ||
            string_util::equal_ignore_case(fsname, "littlefs")) {
            ret = LocalFS.prefix;
            ret += tail;
            return ret;
        }
        if (string_util::equal_ignore_case(fsname, SD.name)) {
            ret = SD.prefix;
            ret += tail;
            return ret;
        }
        ret = defaultFs.prefix;
        ret += filename;
        return ret;
    }

    // The pathname did not begin with /
    ret = defaultFs.prefix;
    ret += "/";
    ret += filename;
    return ret;
}

FluidPath::FluidPath(const std::string_view name, const Volume& fs, std::error_code* ecptr) : std::filesystem::path(canonPath(name, fs)) {
    auto mount = *(++begin());  // Use the path iterator to get the first component
    _isSD      = mount == "sd";

    if (_isSD) {
        std::lock_guard<std::mutex> ref_guard(sd_refcnt_mutex);
        if (!config->_sdCard->config_ok) {
            std::error_code ec = FluidError::SDNotConfigured;
            if (ecptr) {
                *ecptr = ec;
                return;
            }
            throw stdfs::filesystem_error { "SD card is inaccessible", name, ec };
        }
        if (_refcnt == 0) {
            ensure_sd_mount_lock();
            if (sd_mount_lock) {
                xSemaphoreTake(sd_mount_lock, portMAX_DELAY);
            }
            auto ec = sd_mount();
            if (ec) {
                if (sd_mount_lock) {
                    xSemaphoreGive(sd_mount_lock);
                }
                if (ecptr) {
                    *ecptr = ec;
                    return;
                }
                throw stdfs::filesystem_error { "SD card is inaccessible", name, ec };
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
    std::lock_guard<std::mutex> ref_guard(sd_refcnt_mutex);
    if (_isSD && (_refcnt && --_refcnt == 0)) {
        sd_unmount();
        if (sd_mount_lock) {
            xSemaphoreGive(sd_mount_lock);
        }
    }
}
