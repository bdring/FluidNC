#pragma once
#include <string>
#include <map>
#include <filesystem>
#include <cstdint>

class HashFS {
public:
    static bool file_is_hashable(const std::filesystem::path& path);
    static void delete_file(const std::filesystem::path& path, bool report = true);
    static void rehash_file(const std::filesystem::path& path, bool report = true);
    static void rename_file(const std::filesystem::path& ipath, const std::filesystem::path& opath, bool report = true);
    static void hash_all();
    static void enable(bool on);
    static void report_change();

    static std::string hash(const std::filesystem::path& path, bool useCacheOnly = false);

    // A snapshot of all cached hashes, for listing
    static std::map<std::string, std::string> hashes();

private:
    static std::map<std::string, std::string> localFsHashes;
    static bool                               _enabled;
    static uint32_t                           _generation;

    static void        erase_locked(const std::filesystem::path& path);
    static bool        cacheable_locked(const std::filesystem::path& path);
};
