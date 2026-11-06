#include "Driver/localfs.h"
#include "Driver/littlefs.h"  // littlefs_format
#include "FluidPath.h"
#include <cstddef>  // NULL
#include <cstring>
#include <string>
#include "string_util.h"
#include "Config.h"
#include "esp_partition.h"
#include "Logging.h"

// LittleFS lives in a partition named "littlefs" or, on most layouts, one
// named "spiffs" (a quirk of ESP-IDF history; SPIFFS itself is not supported).
const char* spiffsPartition = "spiffs";

const char* littlefsName      = "littlefs";
const char* littlefsPartition = "littlefs";
const char* littlefsPrefix    = "/littlefs";

bool createme = false;

// The partition that holds the local filesystem, or nullptr
static const esp_partition_t* localfs_partition(const char** label) {
    for (const char* name : { littlefsPartition, spiffsPartition }) {
        auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, name);
        if (part) {
            *label = name;
            return part;
        }
    }
    return nullptr;
}

// True if the whole partition is erased flash (all 0xFF), i.e. it holds no
// data and can be formatted without losing anything.  Runs only after a
// failed mount, and stops at the first byte that is not 0xFF.
static bool partition_is_blank(const esp_partition_t* part) {
    uint8_t buf[256];
    for (size_t offset = 0; offset < part->size; offset += sizeof(buf)) {
        if (esp_partition_read(part, offset, buf, sizeof(buf)) != ESP_OK) {
            return false;
        }
        for (auto b : buf) {
            if (b != 0xff) {
                return false;
            }
        }
    }
    return true;
}

// True if block 0 or 1 holds a LittleFS v2 superblock, whose "littlefs"
// magic sits 8 bytes in.  A LittleFS that has this but will not mount is
// damaged beyond use, so reformatting it loses nothing readable.
static bool has_littlefs_magic(const esp_partition_t* part) {
    const size_t blocksize = 4096;
    char         magic[8];
    for (size_t block = 0; block < 2 && (block + 1) * blocksize <= part->size; ++block) {
        if (esp_partition_read(part, block * blocksize + 8, magic, sizeof(magic)) == ESP_OK &&
            memcmp(magic, "littlefs", sizeof(magic)) == 0) {
            return true;
        }
    }
    return false;
}

bool localfs_mount() {
    SD.prefix = "/sd";

    if (littlefs_mount(littlefsPartition, false) == ESP_OK || littlefs_mount(spiffsPartition, false) == ESP_OK) {
        LocalFS.prefix = littlefsPrefix;
        return true;
    }

    const char* label = nullptr;
    auto        part  = localfs_partition(&label);
    if (!part) {
        log_error("No littlefs or spiffs partition for the local filesystem");
        return false;
    }

    // Format erased flash or a damaged LittleFS.  Anything else is probably a
    // SPIFFS filesystem from an older FluidNC release, which this release
    // cannot read but an older one can migrate; formatting it would silently
    // destroy config.yaml.
    if (has_littlefs_magic(part)) {
        log_warn("Local FS is damaged; reformatting");
    } else if (!partition_is_blank(part)) {
        log_error("Local FS is not LittleFS (old SPIFFS?); $LocalFS/Format erases it");
        return false;
    }
    if (littlefs_mount(label, true) == ESP_OK) {
        LocalFS.prefix = littlefsPrefix;
        return true;
    }
    log_error("Cannot create a local filesystem");
    return false;
}

void localfs_unmount() {
    if (LocalFS.prefix == littlefsPrefix) {
        littlefs_unmount();
        return;
    }
    LocalFS.prefix = "";
}

// The local filesystem is always LittleFS; fsname is accepted for
// compatibility with $LocalFS/Format=<name> and otherwise ignored.
bool localfs_format(std::string fsname) {
    localfs_unmount();
    for (const char* label : { littlefsPartition, spiffsPartition }) {
        if (littlefs_format(label) == ESP_OK && littlefs_mount(label) == ESP_OK) {
            LocalFS.prefix = littlefsPrefix;
            return false;
        }
    }
    localfs_mount();
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
