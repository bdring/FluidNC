#pragma once

#include <cstdint>

bool isSPIFFS(const char* mountpoint);
bool isSD(const char* mountpoint);
bool isLittleFS(const char* mountpoint);
bool fluidnc_vfs_stats(const char* mountpoint, uint64_t& total, uint64_t& used);
