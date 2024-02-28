#include "Driver/localfs.h"
#include "Driver/spiffs.h"    // spiffs_format
#include "Driver/littlefs.h"  // littlefs_format
#include <cstddef>            // NULL
#include <cstring>
#include "src/Config.h"
#include "esp_partition.h"

const char* localfsName = NULL;

static bool has_partition(const char* label) {
    auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    return part != NULL;
}

bool localfs_mount() {
    if (has_partition(spiffsName)) {
        if (!spiffs_mount(spiffsName, false)) {
            localfsName = spiffsName;
            return false;
        }
        // Migration - littlefs in spiffs partition
        if (!littlefs_mount(spiffsName, false)) {
            localfsName = littlefsName;
            return false;
        }
        // Try to create a SPIFFS filesystem
        if (!spiffs_mount(spiffsName, true)) {
            localfsName = spiffsName;
            return false;
        }
        log_error("Cannot mount or create a local filesystem in the spiffs partition");
        return true;
    }
    if (has_partition(littlefsName)) {
        // Mount LittleFS, create if necessary
        if (!littlefs_mount(littlefsName, true)) {
            localfsName = littlefsName;
            return false;
        }
        log_error("Cannot mount or create a local filesystem in the littlefs partition");
        return true;
    }
    log_error("The partition map has neither a spiffs partition nor a littlefs partition");
    return true;
}
void localfs_unmount() {
    if (localfsName == spiffsName) {
        spiffs_unmount();
        return;
    }
    if (localfsName == littlefsName) {
        littlefs_unmount();
        return;
    }
    localfsName = NULL;
}
bool localfs_format(const char* fsname) {
    if (!strcasecmp(fsname, "format") || !strcasecmp(fsname, "localfs")) {
        fsname = defaultLocalfsName;
    }
    if (!strcasecmp(fsname, spiffsName)) {
        localfs_unmount();
        if (!spiffs_format(spiffsName)) {
            if (!spiffs_mount(spiffsName)) {
                localfsName = spiffsName;
                return false;
            }
        }
    }
    if (!strcasecmp(fsname, littlefsName)) {
        localfs_unmount();
        if (!littlefs_format(littlefsName)) {
            if (!littlefs_mount(littlefsName)) {
                localfsName = littlefsName;
                return false;
            }
        }
        if (!littlefs_format(spiffsName)) {
            if (!littlefs_mount(spiffsName)) {
                localfsName = littlefsName;
                return false;
            }
        }
        localfs_mount();
        return true;
    }
    localfsName = "";
    return true;
}

uint64_t localfs_size() {
    std::error_code ec;

    auto space = std::filesystem::space(localfsName, ec);
    if (ec) {
        return 0;
    }
    return space.capacity;
}

static void insertFsName(char* s, const char* prefix) {
    size_t slen = strlen(s);
    size_t plen = strlen(prefix);
    memmove(s + 1 + plen, s, slen + 1);
    memmove(s + 1, prefix, plen);
    *s = '/';
}

static bool replacedFsName(char* s, const char* replaced, const char* with) {
    if (*s != '/') {
        return false;
    }

    char*       head = s + 1;
    const char* tail = strchrnul(head, '/');  // tail string after prefix
    size_t      plen = tail - head;           // Prefix length
    size_t      rlen = strlen(replaced);      // replaced length

    if (plen != rlen) {
        return false;
    }

    if (strncasecmp(head, replaced, rlen) == 0) {
        // replaced matches the prefix of s

        size_t tlen = strlen(tail);
        size_t wlen = strlen(with);

        if (wlen != rlen) {
            // Move tail to new location
            memmove(head + wlen, tail, tlen + 1);
        }

        // Insert with at the beginning
        memmove(head, with, wlen);
        return true;
    }
    return false;
}

const char* canonicalPath(const char* filename, const char* defaultFs) {
    static char path[128];
    strncpy(path, filename, 128);

    // Map file system names to canonical form.  The input name is case-independent,
    // while the canonical name is lower case.
    if (!(replacedFsName(path, "localfs", localfsName) || replacedFsName(path, spiffsName, localfsName) ||
          replacedFsName(path, littlefsName, localfsName) ||
          // The following looks like a no-op but it is not because of case independence
          replacedFsName(path, sdName, sdName))) {
        if (*filename != '/') {
            insertFsName(path, "");
        }
        // path now begins with /
        if (!strcmp(defaultFs, "")) {
            // If the default filesystem is empty, insert
            // the local file system prefix
            insertFsName(path, localfsName);
        } else {
            // If the default filesystem is not empty, insert
            // the defaultFs name as the mountpoint name
            insertFsName(path, defaultFs);
        }
    }
    return path;
}
