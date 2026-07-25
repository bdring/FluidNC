// Stages files into the same "native_localfs"/"native_sd" directories
// capture/localfs.cpp already checks for (the same trick the macos/linux/
// windows_x86 ports use: real host directories that appear to FluidNC as
// its local FS and SD card). Here the "real" directories are in
// Emscripten's default in-memory FS (MEMFS), so nothing persists across a
// page reload -- callers are expected to write config.yaml (and anything
// it references, plus any SD-hosted files) before fluidnc_start() calls
// setup(), which is what actually calls localfs_mount()/config->load().
//
// Note that native_sd existing is necessary but not sufficient for
// $SD/List etc. to work: FluidPath.cpp's SDMountState also requires
// config->_sdCard->config_ok, which SDCard::init() only sets once
// config.yaml defines an sdcard: cs_pin (see SDCard.cpp) -- the directory
// alone doesn't configure a card.

#include <emscripten.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
namespace stdfs = std::filesystem;

bool valid_root(const char* root) {
    return root != nullptr && (std::strcmp(root, "native_localfs") == 0 || std::strcmp(root, "native_sd") == 0);
}
}  // namespace

extern "C" {

// root is "native_localfs" or "native_sd"; relpath is relative to it, e.g.
// "config.yaml" or "gcode/test.nc" -- intermediate directories are created
// as needed. Returns true on success.
EMSCRIPTEN_KEEPALIVE
bool fluidnc_stage_file(const char* root, const char* relpath, const char* content) {
    if (!valid_root(root) || relpath == nullptr || content == nullptr || relpath[0] == '\0') {
        return false;
    }
    stdfs::path full = stdfs::path("/") / root / relpath;

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

// Locks every file staged under root read-only. Intended to be called once
// per root, after staging and before fluidnc_start(): since MEMFS never
// persists past a page reload anyway, letting FluidNC believe a settings/
// config write (or an SD-card write) succeeded would be misleading --
// better to fail loudly.
EMSCRIPTEN_KEEPALIVE
void fluidnc_lock_readonly(const char* root) {
    if (!valid_root(root)) {
        return;
    }
    stdfs::path full_root = stdfs::path("/") / root;

    std::error_code ec;
    if (!stdfs::exists(full_root, ec)) {
        return;
    }
    constexpr auto kRead     = stdfs::perms::owner_read | stdfs::perms::group_read | stdfs::perms::others_read;
    constexpr auto kTraverse = stdfs::perms::owner_exec | stdfs::perms::group_exec | stdfs::perms::others_exec;
    // Directories keep the exec ("traversable") bit so their contents stay
    // listable/openable; only files lose write access.
    for (auto& entry : stdfs::recursive_directory_iterator(full_root, ec)) {
        auto perms = entry.is_directory() ? (kRead | kTraverse) : kRead;
        stdfs::permissions(entry.path(), perms, stdfs::perm_options::replace, ec);
    }
    stdfs::permissions(full_root, kRead | kTraverse, stdfs::perm_options::replace, ec);
}

// Ensures root exists even with nothing staged in it (localfs_mount()/
// sd_mount() only check for the directory's existence, e.g. an SD card
// with no config-relevant files still needs native_sd to be a real dir).
EMSCRIPTEN_KEEPALIVE
bool fluidnc_ensure_dir(const char* root) {
    if (!valid_root(root)) {
        return false;
    }
    std::error_code ec;
    stdfs::create_directories(stdfs::path("/") / root, ec);
    return !ec;
}

}  // extern "C"
