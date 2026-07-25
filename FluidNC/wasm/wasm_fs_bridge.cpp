// Stages files into the same "native_localfs" directory capture/localfs.cpp
// already checks for (the same trick the macos/linux/windows_x86 ports use:
// a real host directory that appears to FluidNC as its SD/local FS). Here
// the "real" directory is Emscripten's default in-memory FS (MEMFS), so
// nothing persists across a page reload -- callers are expected to write
// config.yaml (and anything it references) before fluidnc_start() calls
// setup(), which is what actually calls localfs_mount()/config->load().
//
// Deliberately does not touch native_sd: nothing in the core-only scope
// needs it yet (no SD-card-hosted job files), and capture/localfs.cpp's
// sd_mount() is only reached if a config actually configures an SD card.

#include <emscripten.h>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
namespace stdfs = std::filesystem;
constexpr const char* kLocalFsRoot = "/native_localfs";
}  // namespace

extern "C" {

// relpath is relative to native_localfs, e.g. "config.yaml" or
// "boards/mine.yaml" -- intermediate directories are created as needed.
// Returns true on success.
EMSCRIPTEN_KEEPALIVE
bool fluidnc_stage_file(const char* relpath, const char* content) {
    if (relpath == nullptr || content == nullptr || relpath[0] == '\0') {
        return false;
    }
    stdfs::path full = stdfs::path(kLocalFsRoot) / relpath;

    std::error_code ec;
    stdfs::create_directories(full.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << content;
    return out.good();
}

// Locks every file staged so far read-only. Intended to be called once,
// after staging and before fluidnc_start(): since MEMFS never persists
// past a page reload anyway, letting FluidNC believe a settings/config
// write succeeded would be misleading -- better to fail loudly.
EMSCRIPTEN_KEEPALIVE
void fluidnc_lock_localfs_readonly() {
    std::error_code ec;
    if (!stdfs::exists(kLocalFsRoot, ec)) {
        return;
    }
    constexpr auto kRead     = stdfs::perms::owner_read | stdfs::perms::group_read | stdfs::perms::others_read;
    constexpr auto kTraverse = stdfs::perms::owner_exec | stdfs::perms::group_exec | stdfs::perms::others_exec;
    // Directories keep the exec ("traversable") bit so their contents stay
    // listable/openable; only files lose write access.
    for (auto& entry : stdfs::recursive_directory_iterator(kLocalFsRoot, ec)) {
        auto perms = entry.is_directory() ? (kRead | kTraverse) : kRead;
        stdfs::permissions(entry.path(), perms, stdfs::perm_options::replace, ec);
    }
    stdfs::permissions(kLocalFsRoot, kRead | kTraverse, stdfs::perm_options::replace, ec);
}

}  // extern "C"
