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
#include "Arduino.h"

Volume SD { "sd" };
Volume LocalFS { "localfs" };

namespace {
    SemaphoreHandle_t sd_lifecycle_lock = xSemaphoreCreateMutex();
    uint32_t          sd_mount_users    = 0;

    class SDLock {
    public:
        explicit SDLock(SemaphoreHandle_t lock) : _lock(lock) {
            _locked = _lock && xSemaphoreTake(_lock, portMAX_DELAY) == pdTRUE;
        }

        SDLock(const SDLock&)            = delete;
        SDLock& operator=(const SDLock&) = delete;
        SDLock(SDLock&&)                 = delete;
        SDLock& operator=(SDLock&&)      = delete;

        ~SDLock() {
            if (_locked) {
                xSemaphoreGive(_lock);
            }
        }

        bool locked() const { return _locked; }

    private:
        SemaphoreHandle_t _lock   = nullptr;
        bool              _locked = false;
    };

    std::error_code sd_lock_error() {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

// SDMountState manages the SD card mount/unmount lifecycle. FluidPath copies
// share a state, while independently created paths may have separate states.
// The protected user count keeps the card mounted until the last state exits.
SDMountState::SDMountState() {
    SDLock lock(sd_lifecycle_lock);
    if (!lock.locked()) {
        throw stdfs::filesystem_error { "Failed to lock SD card mount state", sd_lock_error() };
    }

    if (sd_mount_users == 0) {
        auto ec = sd_mount();
        if (ec) {
            throw stdfs::filesystem_error { "Failed to mount SD card", ec };
        }
    }
    ++sd_mount_users;
}

SDMountState::~SDMountState() {
    SDLock lock(sd_lifecycle_lock);
    if (lock.locked() && sd_mount_users && --sd_mount_users == 0) {
        sd_unmount();
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

FluidPath::FluidPath(const std::string_view name, const Volume& fs, std::error_code* ecptr) : stdfs::path(canonPath(name, fs)) {
    auto mount = *(++begin());  // Use the path iterator to get the first component
    _isSD = mount == "sd";

    if (_isSD) {
        if (!config->_sdCard->config_ok) {
            std::error_code ec = FluidError::SDNotConfigured;
            if (ecptr) {
                *ecptr = ec;
                return;
            }
            throw stdfs::filesystem_error { "SD card is inaccessible", name, ec };
        }
        try {
            _sd_mount_state = std::make_shared<SDMountState>();
        } catch (const stdfs::filesystem_error& ex) {
            if (ecptr) {
                *ecptr = ex.code();
                return;
            }
            throw stdfs::filesystem_error { "SD card is inaccessible", name, ex.code() };
        }
    }
}
