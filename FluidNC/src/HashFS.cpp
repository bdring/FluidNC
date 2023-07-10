#include "HashFS.h"
#include "FileStream.h"

#include <mbedtls/md.h>

std::map<std::string, std::string> HashFS::localFsHashes;

static char hexNibble(int i) {
    return "0123456789ABCDEF"[i & 0xf];
}

static Error hashFile(const char* ipath, std::string& str) {  // No ESP command
    mbedtls_md_context_t ctx;

    uint8_t shaResult[32];

    try {
        FileStream inFile { ipath, "r" };
        uint8_t    buf[512];
        size_t     len;

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        while ((len = inFile.read(buf, 512)) > 0) {
            mbedtls_md_update(&ctx, buf, len);
        }
        mbedtls_md_finish(&ctx, shaResult);
        mbedtls_md_free(&ctx);
    } catch (const Error err) {
        log_error("Cannot open file " << ipath);
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

void HashFS::rehash() {
    const char*     iDir = "/localfs";
    std::error_code ec;

    localFsHashes.clear();

    FluidPath fpath { iDir, "", ec };
    if (ec) {
        log_error("Cannot open " << iDir);
        return;
    }

    auto iter = stdfs::directory_iterator { fpath, ec };
    if (ec) {
        log_error(fpath << " " << ec.message());
        return;
    }
    Error err = Error::Ok;
    for (auto const& dir_entry : iter) {
        if (dir_entry.is_directory()) {
            log_error("Not handling localfs subdirectories");
        } else {
            std::string ipath(iDir);
            ipath += "/";
            ipath += dir_entry.path().filename();
            std::string hash;
            auto        err1 = hashFile(ipath.c_str(), hash);
            if (err1 != Error::Ok) {
                err = err1;
            } else {
                std::string filename("/");
                filename += dir_entry.path().filename();
                localFsHashes[filename] = hash;
            }
        }
    }
}
std::string HashFS::hash(std::string name) {
    std::map<std::string, std::string>::iterator it;

    it = localFsHashes.find(name);
    if (it != localFsHashes.end()) {
        return it->second;
    }
    return std::string();
}
