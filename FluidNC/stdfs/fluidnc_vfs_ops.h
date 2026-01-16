#pragma once

#include <cstdint>
#include <string_view>

bool fluidnc_vfs_stats(const std::string_view mountpoint, uint64_t& total, uint64_t& used);
