#pragma once
#include <string>
#include <map>
#include <filesystem>

class HashFS {
public:
    static std::map<std::string, std::string> localFsHashes;

    static bool file_is_hashed(const std::filesystem::path& path);
    static void delete_file(const std::filesystem::path& path, bool report = true);
    static void rehash_file(const std::filesystem::path& path, bool report = true);
    static void rename_file(const std::filesystem::path& ipath, const std::filesystem::path& opath, bool report = true);
    static void hash_all();
    static void report_change();

    static std::string hash(const std::filesystem::path& path);

private:
};
