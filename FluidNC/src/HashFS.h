#pragma once
#include <string>
#include <map>
#include <filesystem>

class HashFS {
public:
    static std::map<std::string, std::string> localFsHashes;

    static bool        file_is_hashed(const std::filesystem::path& path);
    static void        delete_file(const std::filesystem::path& path);
    static void        rehash_file(const std::filesystem::path& path);
    static void        hash_all();
    static std::string hash(const std::filesystem::path& path);

private:
};
