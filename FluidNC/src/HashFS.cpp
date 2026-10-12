#include "HashFS.h"
#include "FileStream.h"
#include "SHA256.h"
#include "Driver/watchdog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

std::map<std::string, std::string> HashFS::localFsHashes;
bool                               HashFS::_enabled = false;
uint32_t                           HashFS::_generation = 0;

// The map is used by the web server task and by file
// commands on the protocol task.  The lock is held only for those, never
// across file I/O - hashing a large file on SD takes a while.  Since a
// hash is computed unlocked, every invalidation or store bumps _generation,
// and a hash is only stored if neither happened while it was computed.
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
        int        len;

        SHA256_CTX ctx;
        sha256_init(&ctx);

        // read() returns -1 on an error, which must not become a huge size_t
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

// Only top-level LocalFS files are cached, keyed by bare filename, which
// is what showLocalFSHashes and the WebUI have always seen.
static std::string key(const std::filesystem::path& path) {
    return path.filename().string();
}

// Caller holds the lock
void HashFS::erase_locked(const std::filesystem::path& path) {
    // Only top-level LocalFS files are cached.  Keys are bare filenames, so
    // erasing for any other path could drop a top-level file's hash.  Other
    // paths don't count as invalidations either, so that SD activity does not
    // abort LocalFS hashes in progress.
    if (file_is_hashable(path)) {
        ++_generation;
        localFsHashes.erase(key(path));
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

// Caller holds the lock
bool HashFS::cacheable_locked(const std::filesystem::path& path) {
    return _enabled && file_is_hashable(path);
}

// The hashes are only used by the HTTP server, so there is no point
// computing them unless it is running.
void HashFS::enable(bool on) {
    Lock lock;
    ++_generation;
    _enabled = on;
    if (!on) {
        localFsHashes.clear();
    }
}

void HashFS::rehash_file(const std::filesystem::path& path, bool report) {
    // Retry if something was invalidated while hashing - the file itself
    // may have changed.  Invalidations are rare, so a few tries is plenty,
    // and giving up leaves no hash, which is safe: no ETag, never a stale one.
    bool done = false;
    for (int tries = 0; !done && tries < 3; ++tries) {
        uint32_t generation;
        {
            Lock lock;
            if (!cacheable_locked(path)) {
                erase_locked(path);
                done = true;
                break;
            }
            generation = _generation;
        }
        std::string hash;
        bool        hashed = hashFile(path, hash) == Error::Ok;

        Lock lock;
        if (generation == _generation) {
            if (hashed) {
                // A store counts as an invalidation too: another hash of
                // this file that started earlier, possibly while it was
                // still being written, must not overwrite this one.
                ++_generation;
                localFsHashes[key(path)] = hash;
            } else {
                erase_locked(path);
            }
            done = true;
        }
    }
    if (!done) {
        Lock lock;
        erase_locked(path);  // Do not leave an older hash behind
    }
    if (report) {
        report_change();
    }
}
void HashFS::rename_file(const std::filesystem::path& ipath, const std::filesystem::path& opath, bool report) {
    delete_file(ipath, false);
    rehash_file(opath, report);
}

void HashFS::hash_all() {
    {
        Lock lock;
        ++_generation;
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
}

std::map<std::string, std::string> HashFS::hashes() {
    Lock lock;
    return localFsHashes;
}

std::string HashFS::hash(const std::filesystem::path& path, bool useCacheOnly /*= false*/) {
    bool on;
    {
        Lock lock;
        if (file_is_hashable(path)) {
            auto it = localFsHashes.find(key(path));
            if (it != localFsHashes.end()) {
                return it->second;
            }
        }
        on = _enabled;
    }
    // Top-level LocalFS files are normally all cached, so a miss means the
    // file does not exist - unless caching is disabled, as it is when the
    // HTTP server is not running.
    if ((!on || !file_is_hashable(path)) && !useCacheOnly) {
        std::string theHash;
        hashFile(path, theHash);
        return theHash;
    }
    return std::string();
}
