#include "HashFS.h"
#include "FileStream.h"
#include "SHA256.h"
#include "Driver/watchdog.h"

std::map<std::string, std::string> HashFS::localFsHashes;

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

void HashFS::delete_file(const std::filesystem::path& path, bool report) {
    localFsHashes.erase(path.filename().string());
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

void HashFS::rehash_file(const std::filesystem::path& path, bool report) {
    if (file_is_hashable(path)) {
        std::string hash;
        if (hashFile(path, hash) != Error::Ok) {
            delete_file(path, false);
        } else {
            localFsHashes[path.filename().string()] = hash;
        }
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
    localFsHashes.clear();

    std::error_code ec;
    FluidPath       lfspath { "", LocalFS, ec };
    if (ec) {
        return;
    }

    auto iter = stdfs::directory_iterator { lfspath, ec };
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) {
            log_debug("HashFS: LocalFS unavailable at " << lfspath.string());
        } else {
            log_error(lfspath.string() << " " << ec.message());
        }
        return;
    }
    for (auto const& dir_entry : iter) {
        if (!dir_entry.is_directory()) {
            rehash_file(dir_entry, false);
        }
    }
}
std::string HashFS::hash(const std::filesystem::path& path, bool useCacheOnly /*= false*/) {
    if (file_is_hashable(path)) {
        std::map<std::string, std::string>::const_iterator it;
        it = localFsHashes.find(path.filename().string());
        if (it != localFsHashes.end()) {
            return it->second;
        }
    } else if (!useCacheOnly) {
        std::string theHash;
        hashFile(path, theHash);
        return theHash;
    }
    return std::string();
}
