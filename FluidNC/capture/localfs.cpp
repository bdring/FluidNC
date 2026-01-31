#include "Driver/localfs.h"
#include "Logging.h"
#include "FluidPath.h"
#include <cstdint>
#include <cstring>
#include "string_util.h"
#include <system_error>

#define _GNU_SOURCE

#include <filesystem>
namespace stdfs = std::filesystem;

const char* localfsDir = "./localfs";
const char* sdDir      = "./sd";

bool localfs_format(const std::string fsname) {
    return false;
}
bool localfs_mount() {
    LocalFS.prefix = "native_localfs";
    if (stdfs::is_directory(localfsDir)) {
        return false;
    }
    log_error(localfsDir << " subdirectory is missing");
    return true;
}
void localfs_unmount() {};

std::uintmax_t localfs_size() {
    return 200000;
}

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1) {
    return true;
}
void sd_deinit_slot() {}
void sd_unmount() {}

std::error_code sd_mount(uint32_t max_files) {
    SD.prefix = "native_sd";
    if (stdfs::is_directory(sdDir)) {
        return {};
    }
    log_error(sdDir << " subdirectory is missing");
    return std::make_error_code(std::errc::no_such_file_or_directory);
};
