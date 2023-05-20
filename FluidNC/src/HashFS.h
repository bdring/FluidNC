#pragma once
#include <string>
#include <map>
class HashFS {
public:
    static std::map<std::string, std::string> localFsHashes;

    static void        rehash();
    static std::string hash(std::string name);

private:
};
