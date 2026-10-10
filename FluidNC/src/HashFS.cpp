#include "HashFS.h"
#include "FileStream.h"
#include "SHA256.h"
#include "Driver/watchdog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

std::map<std::string, std::string> HashFS::localFsHashes;
std::string                        HashFS::_uiDir;
bool                               HashFS::_enabled = false;
uint32_t                           HashFS::_generation = 0;

// The map and _uiDir are used by the web server task and by file
// commands on the protocol task.  The lock is held only for those, never
// across file I/O - hashing a large file on SD takes a while.  Since a
// hash is computed unlocked, every invalidation bumps _generation, and a
// hash is only stored if no invalidation happened while it was computed.
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
// showLocalFSHashes and the WebUI have always seen.  Files under
// HTTP/UIDir, which can be on SD, are keyed by full path so they cannot
// collide with a top-level LocalFS name.
std::string HashFS::key(const std::filesystem::path& path) {
    return file_is_hashable(path) ? path.filename().string() : path.string();
}

// Caller holds the lock
void HashFS::erase_locked(const std::filesystem::path& path) {
    ++_generation;
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

// Caller holds the lock
bool HashFS::in_ui_dir_locked(const std::filesystem::path& path) {
    if (_uiDir.empty()) {
        return false;
    }
    return path.string().rfind(_uiDir + "/", 0) == 0;
}

bool HashFS::in_ui_dir(const std::filesystem::path& path) {
    Lock lock;
    return in_ui_dir_locked(path);
}

// Caller holds the lock
bool HashFS::cacheable_locked(const std::filesystem::path& path) {
    return _enabled && (file_is_hashable(path) || in_ui_dir_locked(path));
}

std::string HashFS::ui_dir() {
    Lock lock;
    return _uiDir;
}

// Each WebUI's index.html[.gz] is hashed ahead of time, like top-level
// LocalFS files, so that a reload can be answered with a 304 - including
// during motion - without reading the file.
void HashFS::hash_ui_indexes() {
    auto dir = ui_dir();
    if (dir.empty()) {
        return;
    }
    // The FluidPath keeps an SD volume mounted while hashing
    std::error_code ec;
    FluidPath       fpath { dir, LocalFS, ec };
    if (ec) {
        return;
    }
    auto iter = stdfs::directory_iterator { fpath, ec };
    if (ec) {
        log_debug("HashFS: cannot list " << dir);
        return;
    }
    for (auto const& dir_entry : iter) {
        if (dir_entry.is_directory()) {
            for (const char* name : { "index.html", "index.html.gz" }) {
                auto index = dir_entry.path() / name;
                if (stdfs::exists(index, ec)) {
                    rehash_file(index, false);
                }
            }
        }
    }
}

void HashFS::set_ui_dir(const std::string& path) {
    {
        Lock lock;
        if (path == _uiDir) {
            return;
        }
        ++_generation;
        // Forget the old directory's hashes, so they are neither listed nor
        // reused if it is selected again later.
        if (!_uiDir.empty()) {
            std::string prefix = _uiDir + "/";
            for (auto it = localFsHashes.lower_bound(prefix); it != localFsHashes.end() && it->first.rfind(prefix, 0) == 0;) {
                it = localFsHashes.erase(it);
            }
        }
        _uiDir = path;
    }
    // The indexes are hashed by hash_all() at startup.  After a change,
    // each is hashed on its first request - not here, since this can be
    // called during motion, when the files must not be read.
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
    std::error_code ec;
    if (stdfs::is_directory(opath, ec)) {
        delete_file(opath, false);
        // The renamed directory might be, contain, or be inside HTTP/UIDir
        auto dir = ui_dir() + "/";
        auto o   = opath.string() + "/";
        if (dir != "/" && (dir.rfind(o, 0) == 0 || o.rfind(dir, 0) == 0)) {
            hash_ui_indexes();
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

    hash_ui_indexes();
}

std::map<std::string, std::string> HashFS::hashes() {
    Lock lock;
    return localFsHashes;
}

std::string HashFS::hash(const std::filesystem::path& path, bool useCacheOnly /*= false*/) {
    bool     on;
    uint32_t generation;
    {
        Lock lock;
        auto it = localFsHashes.find(key(path));
        if (it != localFsHashes.end()) {
            return it->second;
        }
        on         = _enabled;
        generation = _generation;
    }
    // Top-level LocalFS files are normally all cached, so a miss means the
    // file does not exist - unless caching is disabled.
    if ((!on || !file_is_hashable(path)) && !useCacheOnly) {
        std::string theHash;
        if (hashFile(path, theHash) == Error::Ok && on) {
            // First request for a file under HTTP/UIDir, or an index
            // missed because the card was absent at startup.
            Lock lock;
            if (generation == _generation && cacheable_locked(path)) {
                localFsHashes[key(path)] = theHash;
            }
        }
        return theHash;
    }
    return std::string();
}
