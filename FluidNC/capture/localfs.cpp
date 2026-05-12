#include "Driver/localfs.h"
#include "Driver/fluidnc_gpio.h"  // pinnum_t
#include "Logging.h"
#include "FluidPath.h"
#include <cstdint>
#include <cstring>
#include "string_util.h"
#include <system_error>
#include <iostream>

#define _GNU_SOURCE

#include <filesystem>
namespace stdfs = std::filesystem;

bool localfs_format(const std::string fsname) {
    return false;
}
bool localfs_mount() {
    SD.prefix      = "native_sd";
    LocalFS.prefix = "native_localfs";
    if (stdfs::is_directory(LocalFS.prefix)) {
        return true;
    }
    log_error(LocalFS.prefix << " subdirectory is missing");
    return false;
}
void localfs_unmount() {};

std::uintmax_t localfs_size() {
    return 200000;
}

bool sd_init_slot(uint32_t freq_hz, pinnum_t cs_pin, pinnum_t cd_pin = -1, pinnum_t wp_pin = -1) {
    return true;
}
void sd_deinit_slot() {}
void sd_unmount() {}

std::error_code sd_mount(uint32_t max_files) {
    std::cout << "Mounting sd from " << SD.prefix << std::endl;

    if (stdfs::is_directory(SD.prefix)) {
        return {};
    }
    log_error(SD.prefix << " subdirectory is missing");
    return std::make_error_code(std::errc::no_such_file_or_directory);
};
