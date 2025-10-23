#include "Driver/localfs.h"
#include "Logging.h"
#include <cstdint>
#include <cstring>
#include <system_error>

#define _GNU_SOURCE

#include <filesystem>
namespace stdfs = std::filesystem;

const char* localfsName = "localfs";
const char* localfsDir  = "native_localfs/";
const char* sdDir       = "native_sd/";

bool localfs_format(const char* fsname) {
    return false;
}
bool localfs_mount() {
    if (stdfs::is_directory(localfsDir)) {
        return false;
    }
    log_error(localfsDir << " subdirectory is missing");
    return true;
}
void localfs_unmount() {};

std::uintmax_t localfs_size() {
    return 200000;
}

static void insertFsName(char* s, const char* prefix) {
    size_t slen = strlen(s);
    size_t plen = strlen(prefix);
    memmove(s + plen, s, slen);
    memmove(s, prefix, plen);
}

// Some compilers do not have strchrnul
static char* my_strchrnul(const char* s, int c) {
    while (*s != c && *s != '\0') {
        s++;
    }
    return (char*)s;
}
static bool replacedFsName(char* s, const char* replaced, const char* with) {
    if (*s != '/') {
        return false;
    }

    char*       head = s + 1;
    const char* tail = my_strchrnul(head, '/');  // tail string after prefix
    size_t      plen = tail - head;              // Prefix length
    size_t      rlen = strlen(replaced);         // replaced length

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
    if (!(replacedFsName(path, "localfs", localfsDir) || replacedFsName(path, "sd", sdDir))) {
        // We did not succeed in replacing a /localfs or /sd prefix, so we insert
        // the default

        if (strcmp(defaultFs, "") == 0 || strcmp(defaultFs, "localfs") == 0) {
            // If the default filesystem is empty, insert
            // the local file system prefix
            insertFsName(path, localfsDir);
        } else if (strcmp(defaultFs, sdName) == 0) {
            // Insert the SD directory name
            insertFsName(path, sdDir);
        }
    }

    return path;
}

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1) {
    return true;
}
void sd_deinit_slot() {}
void sd_unmount() {}

std::error_code sd_mount(uint32_t max_files) {
    if (stdfs::is_directory(sdDir)) {
        return {};
    }
    log_error(sdDir << " subdirectory is missing");
    return std::make_error_code(std::errc::no_such_file_or_directory);
};
