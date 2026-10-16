// Copyright (c) 2026 - FluidNC Contributors
// Use of this source code be governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <filesystem>
#include <system_error>

// What the web handlers need to know about a path, from a non-throwing
// std::filesystem::status() call.  Those handlers run in the async web
// server task, where nothing catches an exception, so they cannot use the
// throwing overloads: an SD card that fails to answer would abort the
// firmware instead of producing an HTTP error.
enum class FsProbe {
    NONE,         // nothing usable at that path: missing, a path error, or neither file nor directory
    FILE,         // a regular file
    DIR,          // a directory
    UNAVAILABLE,  // the volume itself did not answer (I/O error, no device, timeout)
};

// Whether an error from the filesystem means the volume is failing rather
// than the path being wrong.  These are the errno values esp_vfs_fat uses for
// FR_DISK_ERR, FR_NOT_READY and FR_TIMEOUT.  A client should retry those later
// (HTTP 503); a path error (EACCES, ENAMETOOLONG, ...) is permanent and is
// answered like a missing resource, so a client that retries 503 as transient
// does not loop on it.
inline bool fs_volume_unavailable(const std::error_code& ec) {
    return ec == std::errc::io_error || ec == std::errc::no_such_device || ec == std::errc::timed_out;
}

// Classify the result of std::filesystem::status(path, ec).
//
// libstdc++ reports ENOENT and ENOTDIR as file_type::not_found, and every
// other failure as file_type::none; ec carries the errno in both cases, so
// the type has to be looked at before the error code.  A missing path is a
// normal answer here (PUT and MKCOL create it), so it is NONE rather than an
// error.
inline FsProbe fs_probe(const std::filesystem::file_status& st, const std::error_code& ec) {
    if (st.type() == std::filesystem::file_type::not_found) {
        return FsProbe::NONE;
    }
    if (ec) {
        return fs_volume_unavailable(ec) ? FsProbe::UNAVAILABLE : FsProbe::NONE;
    }
    if (std::filesystem::is_regular_file(st)) {
        return FsProbe::FILE;
    }
    if (std::filesystem::is_directory(st)) {
        return FsProbe::DIR;
    }
    return FsProbe::NONE;
}
