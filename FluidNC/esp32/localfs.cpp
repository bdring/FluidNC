#include "Driver/localfs.h"
#include "Driver/spiffs.h"    // spiffs_format
#include "Driver/littlefs.h"  // littlefs_format
#include "FluidPath.h"
#include <cstddef>  // NULL
#include <cstring>
#include <string>
#include "string_util.h"
#include "Config.h"
#include "esp_partition.h"
#include "Logging.h"

const char* spiffsName      = "spiffs";
const char* spiffsPartition = "spiffs";
const char* spiffsPrefix    = "/spiffs";

const char* littlefsName      = "littlefs";
const char* littlefsPartition = "littlefs";
const char* littlefsPrefix    = "/littlefs";

bool createme = false;

bool localfs_mount() {
    SD.prefix = "/sd";

    if (spiffs_mount(spiffsPartition, false) == ESP_OK) {
        LocalFS.prefix = spiffsPrefix;
        return true;
    }
    // Mount LittleFS, create if necessary
    if (littlefs_mount(littlefsPartition, true) == ESP_OK || littlefs_mount(spiffsPartition, true) == ESP_OK) {
        LocalFS.prefix = littlefsPrefix;
        return true;
    }
    log_error("Cannot mount or create a local filesystem");
    return false;
}

void localfs_unmount() {
    if (LocalFS.prefix == spiffsPrefix) {
        spiffs_unmount();
        return;
    }
    if (LocalFS.prefix == littlefsPrefix) {
        littlefs_unmount();
        return;
    }
    LocalFS.prefix = "";
}

bool localfs_format(std::string fsname) {
    if (fsname != "format" || fsname != LocalFS.name) {
        fsname = littlefsName;  // Default to littlefs
    }
    if (fsname == spiffsName) {
        localfs_unmount();
        if (spiffs_format(spiffsPartition) == ESP_OK) {
            if (spiffs_mount(spiffsPartition) == ESP_OK) {
                LocalFS.prefix = spiffsPrefix;
                return false;
            }
        }
    }
    if (fsname == littlefsName) {
        localfs_unmount();
        // First try to create a LittleFS filesystm in a partition named "littlefs"
        if (littlefs_format(littlefsPartition) == ESP_OK) {
            if (littlefs_mount(littlefsPartition) == ESP_OK) {
                LocalFS.prefix = littlefsPrefix;
                return false;
            }
        }
        // Failing that, try to create it in a partition named "spiffs".
        // This is due to a quirk of ESP-IDF history.
        if (littlefs_format(spiffsPartition) == ESP_OK) {
            if (littlefs_mount(spiffsPartition) == ESP_OK) {
                LocalFS.prefix = littlefsPrefix;
                return false;
            }
        }
        localfs_mount();
        return true;
    }
    LocalFS.prefix = "";
    return true;
}

uint64_t localfs_size() {
    std::error_code ec;

    auto space = std::filesystem::space(LocalFS.prefix, ec);
    if (ec) {
        return 0;
    }
    return space.capacity;
}
