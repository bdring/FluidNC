#pragma once
#include <filesystem>

bool localfs_format(const char* fsname);
bool localfs_mount();
void localfs_unmount();

const char* canonicalPath(const char* filename, const char* defaultFs);

std::uintmax_t localfs_size();

extern const char* localFsName;

constexpr const char* spiffsName   = "spiffs";
constexpr const char* littlefsName = "littlefs";

constexpr const char* defaultLocalFsName  = spiffsName;
constexpr const char* actualLocalFsPrefix = "/spiffs";
