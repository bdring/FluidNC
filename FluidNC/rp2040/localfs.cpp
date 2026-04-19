// Copyright 2025 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/localfs.h"
#include "FluidPath.h"
#include "VFS.h"
#include <LittleFS.h>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include "Logging.h"

namespace stdfs = std::filesystem;

// RP2040 LittleFS filesystem using flash memory
// Typically uses the last 256KB of flash for filesystem
// This assumes 2MB flash (standard for Raspberry Pi Pico)

bool localfs_mount() {
    FSConfig cfg = FSConfig();
    cfg.setAutoFormat(true);
    LittleFS.setConfig(cfg);
    if (!LittleFS.begin()) {
        while (1) {
            log_error("LittleFS mount failed. Did you forget to upload a filesystem image?");
            delay(5000);
        }
    }

    // Initialize LittleFS - this should mount automatically on RP2040
    // with Arduino framework
    if (!LittleFS.begin()) {
        log_error("Failed to mount LittleFS");

        return false;
    }

    // Set LittleFS as the root filesystem for VFS layer
    VFS.root(LittleFS);

    LocalFS.prefix = "/littlefs";  // LittleFS is mounted at root on RP2040
    VFS.map("/littlefs", LittleFS);

    SD.prefix = "/sd";  // SD card would go here if used

    log_info("LittleFS mounted successfully at " << LocalFS.prefix);
    return true;
}

void localfs_unmount() {
    // LittleFS doesn't have an explicit unmount on RP2040
    // The filesystem stays mounted for the lifetime of the device
}

bool localfs_format(const std::string fsname) {
    // Format LittleFS filesystem
    // This will erase all data!

    log_warn("Formatting LittleFS - this will erase all data!");

    if (!LittleFS.format()) {
        log_error("LittleFS format failed");
        return false;
    }

    // Re-mount after format
    if (!LittleFS.begin()) {
        log_error("Failed to remount LittleFS after format");
        return false;
    }

    log_info("LittleFS formatted successfully");
    return true;
}

std::uintmax_t localfs_size() {
    // Get LittleFS total size using std::filesystem if available
    // Otherwise, use FS::info() from Arduino LittleFS library

    std::error_code ec;
    try {
        auto space = stdfs::space("/", ec);
        if (!ec) {
            return space.capacity;
        }
    } catch (...) {
        // std::filesystem::space might not work with LittleFS on RP2040
    }

    // Fallback: Use Arduino LittleFS info() to get actual filesystem size
    FSInfo fsinfo;
    LittleFS.info(fsinfo);

    // fsinfo.totalBytes tells us the total capacity
    if (fsinfo.totalBytes > 0) {
        return fsinfo.totalBytes;
    }

    // Final fallback: RP2040 typically allocates ~256KB for LittleFS
    // (last 256KB of 2MB flash)
    return 256 * 1024;  // 256KB default
}
