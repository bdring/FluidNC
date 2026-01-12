#pragma once
#include <filesystem>
#include <string>

bool localfs_format(const std::string fsname);
bool localfs_mount();
void localfs_unmount();

std::uintmax_t localfs_size();
