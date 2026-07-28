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
#include <sstream>
#include <system_error>

namespace {
namespace stdfs = std::filesystem;

bool valid_root(const char* root) {
    return root != nullptr && (std::strcmp(root, "native_localfs") == 0 || std::strcmp(root, "native_sd") == 0);
}

// Minimal JSON string escaping -- just enough for filenames, which won't
// contain most of the characters that need escaping, but could plausibly
// contain a quote or backslash.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += c;
        }
    }
    return out;
}

// Reused/overwritten on every call rather than heap-allocated per call:
// callers (Module.cwrap with a 'string' return type) read the string
// synchronously before the next bridge call can overwrite it.
std::string g_result_buffer;

// Resolves root+relpath and rejects anything that would escape root (e.g.
// a ".." segment). Not a hard security boundary -- the WebUI and the WASM
// instance already share the same JS heap -- just protection against a
// client-side bug reaching outside the intended directory.
//
// relpath is WebUI's POSIX-absolute-style path ("/" for root, "/sub/f.nc");
// the leading slash is stripped before appending, since std::filesystem's
// operator/ treats an absolute right-hand side as replacing the whole
// path rather than appending to it.
bool safe_path(const char* root, const char* relpath, stdfs::path& out) {
    if (!valid_root(root) || relpath == nullptr) {
        return false;
    }
    std::string rel = relpath;
    while (!rel.empty() && rel.front() == '/') {
        rel.erase(rel.begin());
    }
    stdfs::path full_root = stdfs::path("/") / root;
    stdfs::path full       = rel.empty() ? full_root : full_root / rel;

    std::error_code ec;
    stdfs::path canon_root = stdfs::weakly_canonical(full_root, ec);
    stdfs::path canon_full = stdfs::weakly_canonical(full, ec);
    auto        match      = std::mismatch(canon_root.begin(), canon_root.end(), canon_full.begin(), canon_full.end());
    if (match.first != canon_root.end()) {
        return false;
    }
    out = full;
    return true;
}
}  // namespace

extern "C" {

// root is "native_localfs" or "native_sd"; relpath is relative to it, e.g.
// "config.yaml" or "gcode/test.nc" -- intermediate directories are created
// as needed. Returns true on success.
EMSCRIPTEN_KEEPALIVE
bool fluidnc_stage_file(const char* root, const char* relpath, const char* content) {
    if (content == nullptr || relpath == nullptr || relpath[0] == '\0') {
        return false;
    }
    stdfs::path full;
    if (!safe_path(root, relpath, full)) {
        return false;
    }

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

// Lists the immediate children of root+relpath, matching the shape
// WebUI-mm's DIRECTSD-source.ts/FLASH-source.ts formatResult() expects:
// {"files":[{"name":...,"size":...}...],"path":...,"total":...,"used":...,
//  "occupation":...,"status":"Ok"}. size:-1 (not a separate isDir field)
// is what marks an entry as a directory -- see components/Helpers/
// filters.ts's sortedFilesList(). "total" is a nominal capacity, not a
// real quota -- MEMFS doesn't have one to report.
EMSCRIPTEN_KEEPALIVE
const char* fluidnc_fs_list(const char* root, const char* relpath) {
    constexpr unsigned long long kNominalCapacity = 16 * 1024 * 1024;

    stdfs::path dir;
    std::error_code ec;
    if (!safe_path(root, relpath, dir) || !stdfs::is_directory(dir, ec)) {
        g_result_buffer = R"({"files":[],"path":"/","total":"0","used":"0","occupation":"0","status":"error"})";
        return g_result_buffer.c_str();
    }

    std::ostringstream files_json;
    bool                first = true;
    unsigned long long  used  = 0;
    for (auto& entry : stdfs::directory_iterator(dir, ec)) {
        if (!first) {
            files_json << ",";
        }
        first          = false;
        bool is_dir    = entry.is_directory(ec);
        auto size      = is_dir ? 0 : static_cast<long long>(entry.file_size(ec));
        used += is_dir ? 0 : size;
        files_json << "{\"name\":\"" << json_escape(entry.path().filename().string()) << "\","
                   << "\"size\":" << (is_dir ? -1 : size) << ",\"datetime\":\"\"}";
    }

    std::ostringstream out;
    out << "{\"files\":[" << files_json.str() << "],"
        << "\"path\":\"" << json_escape(relpath ? relpath : "/") << "\","
        << "\"total\":\"" << kNominalCapacity << "\","
        << "\"used\":\"" << used << "\","
        << "\"occupation\":\"" << (used * 100 / kNominalCapacity) << "\","
        << "\"status\":\"Ok\"}";
    g_result_buffer = out.str();
    return g_result_buffer.c_str();
}

EMSCRIPTEN_KEEPALIVE
bool fluidnc_fs_delete(const char* root, const char* relpath) {
    stdfs::path p;
    std::error_code ec;
    if (!safe_path(root, relpath, p) || !stdfs::is_regular_file(p, ec)) {
        return false;
    }
    return stdfs::remove(p, ec) && !ec;
}

EMSCRIPTEN_KEEPALIVE
bool fluidnc_fs_deletedir(const char* root, const char* relpath) {
    stdfs::path p;
    std::error_code ec;
    if (!safe_path(root, relpath, p) || !stdfs::is_directory(p, ec)) {
        return false;
    }
    stdfs::remove_all(p, ec);
    return !ec;
}

EMSCRIPTEN_KEEPALIVE
bool fluidnc_fs_mkdir(const char* root, const char* relpath) {
    stdfs::path p;
    if (!safe_path(root, relpath, p)) {
        return false;
    }
    std::error_code ec;
    stdfs::create_directories(p, ec);
    return !ec;
}

// Returns the file's content, or "" if it doesn't exist/isn't readable --
// download only ever gets called on paths the file listing already showed
// exist, so that ambiguity (vs. a genuinely empty file) isn't worth a
// separate exists check here.
EMSCRIPTEN_KEEPALIVE
const char* fluidnc_fs_read(const char* root, const char* relpath) {
    stdfs::path p;
    std::error_code ec;
    if (!safe_path(root, relpath, p) || !stdfs::is_regular_file(p, ec)) {
        g_result_buffer.clear();
        return g_result_buffer.c_str();
    }
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        g_result_buffer.clear();
        return g_result_buffer.c_str();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    g_result_buffer = ss.str();
    return g_result_buffer.c_str();
}

}  // extern "C"
