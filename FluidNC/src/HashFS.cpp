#include "HashFS.h"
#include "FileStream.h"
#include "SHA256.h"
#include "Driver/watchdog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

std::map<std::string, std::string> HashFS::localFsHashes;
std::string                        HashFS::_indexFile;
bool                               HashFS::_enabled = false;

// The map and _indexFile are used by the web server task and by file
// commands on the protocol task.  The lock is held only for those, never
// across file I/O - hashing a large file on SD takes a while.
namespace {
    SemaphoreHandle_t hash_mutex = xSemaphoreCreateMutex();

    struct Lock {
        Lock() { xSemaphoreTake(hash_mutex, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(hash_mutex); }
    };
}

static char hexNibble(uint8_t i) {
    return "0123456789ABCDEF"[i & 0xf];
}

static Error hashFile(const std::filesystem::path& ipath, std::string& str) {  // No ESP command
    uint8_t shaResult[32];

    try {
        FileStream inFile { ipath.string(), "r" };
        uint8_t    buf[512];
        size_t     len;

        SHA256_CTX ctx;
        sha256_init(&ctx);

        while ((len = inFile.read(buf, 512)) > 0) {
            sha256_update(&ctx, buf, len);
            feed_watchdog();
        }
        sha256_final(&ctx, shaResult);
    } catch (const ErrorException& err) {
        log_debug("Cannot hash file " << ipath.string());
        return Error::FsFailedOpenFile;
    }

    str = '"';
    for (int i = 0; i < 32; i++) {
        uint8_t b = shaResult[i];
        str += hexNibble(b >> 4);
        str += hexNibble(b);
    }
    str += '"';

    return Error::Ok;
}

void HashFS::report_change() {
    log_msg("Files changed");
}

// Top-level LocalFS files are keyed by bare filename, which is what
// showLocalFSHashes and the WebUI have always seen.  HTTP/IndexFile,
// which can be in a subdirectory or on SD, is keyed by full path so it
// cannot collide with the LocalFS index.html it stands in for.
std::string HashFS::key(const std::filesystem::path& path) {
    return file_is_hashable(path) ? path.filename().string() : path.string();
}

// Caller holds the lock
void HashFS::erase_locked(const std::filesystem::path& path) {
    localFsHashes.erase(key(path));

    // The path might be a directory that was removed or renamed, so drop
    // any full-path entries beneath it.
    std::string prefix = path.string() + "/";
    for (auto it = localFsHashes.lower_bound(prefix); it != localFsHashes.end() && it->first.rfind(prefix, 0) == 0;) {
        it = localFsHashes.erase(it);
    }
}

void HashFS::delete_file(const std::filesystem::path& path, bool report) {
    {
        Lock lock;
        erase_locked(path);
    }
    if (report) {
        report_change();
    }
}

bool HashFS::file_is_hashable(const std::filesystem::path& path) {
    uint32_t count = 0;
    for (auto it = path.begin(); it != path.end(); ++it) {
        ++count;
    }
    // The first component is "/", then e.g. "littlefs", then
    // the filename.  If there are more components, there is
    // a subdirectory and we do not hash it.
    if (count != 3) {
        return false;
    }
    auto fsname = *++path.begin();
    return fsname == "littlefs" || fsname == "spiffs" || fsname == "localfs";
}

std::string HashFS::index_file() {
    Lock lock;
    return _indexFile;
}

// HTTP/IndexFile is hashed ahead of time, like top-level LocalFS files,
// so that WebUI can answer If-None-Match - including during motion -
// without reading it.  It is stored without any .gz suffix, and either
// form of the file might be the one served.
bool HashFS::is_index_file(const std::filesystem::path& path) {
    auto index = index_file();
    if (index.empty()) {
        return false;
    }
    auto s = path.string();
    return s == index || s == index + ".gz";
}

void HashFS::hash_index_file() {
    auto index = index_file();
    if (index.empty()) {
        return;
    }
    // The FluidPath keeps an SD volume mounted while hashing
    std::error_code ec;
    FluidPath       fpath { index, LocalFS, ec };
    if (!ec) {
        rehash_file(fpath, false);
        fpath += ".gz";
        rehash_file(fpath, false);
    }
}

void HashFS::set_index_file(const std::string& path) {
    Lock lock;
    if (path == _indexFile) {
        return;
    }
    // Forget the old index file's hashes, so they are neither listed nor
    // reused if it is selected again after changing.  If it was a top-level
    // LocalFS file, its entry is an ordinary LocalFS hash and stays.
    if (!_indexFile.empty()) {
        std::filesystem::path old(_indexFile);
        if (!file_is_hashable(old)) {
            localFsHashes.erase(_indexFile);
            localFsHashes.erase(_indexFile + ".gz");
        }
    }
    _indexFile = path;
}

// The hashes are only used by the HTTP server, so there is no point
// computing them unless it is running.
void HashFS::enable(bool on) {
    Lock lock;
    _enabled = on;
    if (!on) {
        localFsHashes.clear();
    }
}

bool HashFS::enabled() {
    Lock lock;
    return _enabled;
}

void HashFS::rehash_file(const std::filesystem::path& path, bool report) {
    std::string hash;
    bool        hashed = enabled() && (file_is_hashable(path) || is_index_file(path)) && hashFile(path, hash) == Error::Ok;
    {
        Lock lock;
        if (hashed) {
            localFsHashes[key(path)] = hash;
        } else {
            erase_locked(path);
        }
    }
    if (report) {
        report_change();
    }
}
void HashFS::rename_file(const std::filesystem::path& ipath, const std::filesystem::path& opath, bool report) {
    delete_file(ipath, false);
    std::error_code ec;
    if (stdfs::is_directory(opath, ec)) {
        delete_file(opath, false);
        if (index_file().rfind(opath.string() + "/", 0) == 0) {
            hash_index_file();
        }
        if (report) {
            report_change();
        }
    } else {
        rehash_file(opath, report);
    }
}

void HashFS::hash_all() {
    {
        Lock lock;
        localFsHashes.clear();
        if (!_enabled) {
            return;
        }
    }

    std::error_code ec;
    {
        FluidPath lfspath { "", LocalFS, ec };
        if (!ec) {
            auto iter = stdfs::directory_iterator { lfspath, ec };
            if (ec) {
                if (ec == std::errc::no_such_file_or_directory) {
                    log_debug("HashFS: LocalFS unavailable at " << lfspath.string());
                } else {
                    log_error(lfspath.string() << " " << ec.message());
                }
            } else {
                for (auto const& dir_entry : iter) {
                    if (!dir_entry.is_directory()) {
                        rehash_file(dir_entry, false);
                    }
                }
            }
        }
    }

    hash_index_file();
}

std::map<std::string, std::string> HashFS::hashes() {
    Lock lock;
    return localFsHashes;
}

std::string HashFS::hash(const std::filesystem::path& path, bool useCacheOnly /*= false*/) {
    bool on;
    {
        Lock lock;
        auto it = localFsHashes.find(key(path));
        if (it != localFsHashes.end()) {
            return it->second;
        }
        on = _enabled;
    }
    // Top-level LocalFS files are normally all cached, so a miss means the
    // file does not exist - unless caching is disabled.
    if ((!on || !file_is_hashable(path)) && !useCacheOnly) {
        std::string theHash;
        if (hashFile(path, theHash) == Error::Ok && on && is_index_file(path)) {
            // Missed in advance because the setting just changed or the
            // card was absent at startup.
            Lock lock;
            localFsHashes[key(path)] = theHash;
        }
        return theHash;
    }
    return std::string();
}
