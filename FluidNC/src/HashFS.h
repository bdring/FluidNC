#pragma once
#include <string>
#include <map>
#include <filesystem>

class HashFS {
public:
    static std::map<std::string, std::string> localFsHashes;

    static bool file_is_hashable(const std::filesystem::path& path);
    static void delete_file(const std::filesystem::path& path, bool report = true);
    static void rehash_file(const std::filesystem::path& path, bool report = true);
    static void rename_file(const std::filesystem::path& ipath, const std::filesystem::path& opath, bool report = true);
    static void hash_all();
    static void enable(bool on);
    static void report_change();

    static std::string hash(const std::filesystem::path& path, bool useCacheOnly = false);

    // Canonical path of HTTP/IndexFile, without any .gz suffix.  It and
    // its .gz form are hashed in advance.  Empty for none.
    static void set_index_file(const std::string& path);
    static bool is_index_file(const std::filesystem::path& path);

private:
    static std::string _indexFile;
    static bool        _enabled;

    static std::string key(const std::filesystem::path& path);
    static void        hash_index_file();
};
