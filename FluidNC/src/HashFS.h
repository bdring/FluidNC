#pragma once
#include <string>
#include <map>
#include <filesystem>

class HashFS {
public:
    static std::map<std::string, std::string> localFsHashes;

    static void        rehash_file(const std::filesystem::path& path);
    static void        rehash();
    static std::string hash(std::string name);

private:
};
