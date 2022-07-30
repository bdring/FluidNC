#include "Driver/localfs.h"
#include "Driver/spiffs.h"    // spiffs_format
#include "Driver/littlefs.h"  // littlefs_format
#include <cstddef>            // NULL
#include <cstring>
#include "src/Logging.h"

const char* localFsName = NULL;

bool localfs_mount() {
    if (defaultLocalFsName == spiffsName) {
        if (!spiffs_mount()) {
            localFsName = spiffsName;
            return false;
        }
        if (!littlefs_mount()) {
            localFsName = littlefsName;
            return false;
        }
    } else {
        if (!littlefs_mount()) {
            localFsName = littlefsName;
            return false;
        }
        if (!spiffs_mount()) {
            localFsName = spiffsName;
            return false;
        }
    }
    return true;
}
void localfs_unmount() {
    if (localFsName == spiffsName) {
        spiffs_unmount();
        return;
    }
    if (localFsName == littlefsName) {
        littlefs_unmount();
        return;
    }
    localFsName = NULL;
}
bool localfs_format(const char* fsname) {
    if (!strcasecmp(fsname, "format") || !strcasecmp(fsname, "localfs")) {
        fsname = defaultLocalFsName;
    }
    if (!strcasecmp(fsname, spiffsName)) {
        localfs_unmount();
        if (spiffs_format(spiffsName)) {
            return true;
        }
        return spiffs_mount();
    }
    if (!strcasecmp(fsname, littlefsName)) {
        localfs_unmount();
        if (littlefs_format(littlefsName)) {
            return true;
        }
        return littlefs_mount();
    }
    return true;
}

uint64_t localfs_size() {
    std::error_code ec;

    auto space = std::filesystem::space(localFsName, ec);
    if (ec) {
        return 0;
    }
    return space.capacity;
}

static void insertString(char* s, const char* prefix) {
    size_t slen = strlen(s);
    size_t plen = strlen(prefix);
    memmove(s + plen, s, slen + 1);
    memmove(s, prefix, plen);
}

static bool replacedInitialSubstring(char* s, const char* replaced, const char* with) {
    if (*s == '\0') {
        return false;
    }

    const char* tail = strchrnul(s + 1, '/');  // tail string after prefix
    size_t      plen = tail - s;               // Prefix length
    size_t      rlen = strlen(replaced);       // replaced length

    if (plen != rlen) {
        return false;
    }

    if (strncasecmp(s, replaced, rlen) == 0) {
        // replaced matches the prefix of s

        size_t tlen = strlen(tail);
        size_t wlen = strlen(with);

        if (wlen > rlen) {
            // Slide tail to the right
            memmove(s + rlen, tail, tlen + 1);
        }
        // Insert with at the beginning
        memmove(s, with, wlen);

        if (wlen < rlen) {
            // Slide tail to the left
            memmove(s + wlen, tail, tlen + 1);
        }
        return true;
    }
    return false;
}

const char* canonicalPath(const char* filename, const char* defaultFs) {
    const char* spiffsPrefix   = "/spiffs";
    const char* sdPrefix       = "/sd";
    const char* localFsPrefix  = "/localfs";
    const char* littleFsPrefix = "/littlefs";

    static char path[128];
    strncpy(path, filename, 128);

    // Map file system names to canonical form.  The input name is case-independent,
    // while the canonical name is lower case.
    if (!(replacedInitialSubstring(path, localFsPrefix, actualLocalFsPrefix) ||
          replacedInitialSubstring(path, spiffsPrefix, actualLocalFsPrefix) ||
          replacedInitialSubstring(path, littleFsPrefix, actualLocalFsPrefix) ||
          // The following looks like a no-op but it is not because of case independence
          replacedInitialSubstring(path, sdPrefix, sdPrefix))) {
        if (*filename != '/') {
            insertString(path, "/");
        }
        // path now begins with /
        if (!strcmp(defaultFs, "")) {
            // If the default filesystem is empty, insert
            // the local file system prefix
            insertString(path, actualLocalFsPrefix);
        } else {
            // If the default filesystem is not empty, insert
            // the defaultFs name as the mountpoint name
            insertString(path, defaultFs);
        }
    }
    return path;
}
