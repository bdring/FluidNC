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

static SemaphoreHandle_t sd_mount_lock = nullptr;

static void ensure_sd_locks() {
    if (!sd_mount_lock) {
        sd_mount_lock = xSemaphoreCreateBinary();
        if (sd_mount_lock) {
            xSemaphoreGive(sd_mount_lock);
        }
    }
}

// SDMountState manages SD card mount/unmount lifecycle
// It is instantiated as a shared_ptr that is automatically
// reference-counted.  The constructor is called on the
// first instance, thus mounting the SD card.  The destructor
// is called when the count goes to 0.
SDMountState::SDMountState() {
    ensure_sd_locks();
    xSemaphoreTake(sd_mount_lock, portMAX_DELAY);
    auto ec = sd_mount();
    xSemaphoreGive(sd_mount_lock);
    if (ec) {
        throw stdfs::filesystem_error { "Failed to mount SD card", ec };
    }
}

SDMountState::~SDMountState() {
    // Use 100ms timeout to avoid deadlock if operations are still in progress
    //    if (xSemaphoreTake(sd_mount_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (xSemaphoreTake(sd_mount_lock, portMAX_DELAY)) {
        sd_unmount();
        xSemaphoreGive(sd_mount_lock);
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
    ensure_sd_locks();
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
            // Try to reuse existing mount state if another FluidPath still owns it
            static std::weak_ptr<SDMountState> cached_mount;
            if (auto mount = cached_mount.lock()) {
                log_info("was lock");
                _sd_mount_state = mount;
            } else {
                log_info("make shared");
                _sd_mount_state = std::make_shared<SDMountState>();
                cached_mount    = _sd_mount_state;
            }
        } catch (const stdfs::filesystem_error& ex) {
            if (ecptr) {
                *ecptr = ex.code();
                return;
            }
            throw stdfs::filesystem_error { "SD card is inaccessible", name, ex.code() };
        }
    }
}
