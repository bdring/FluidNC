#pragma once

#include <cstdint>
#include <system_error>

bool usb_init_host();
std::error_code usb_mount(uint32_t max_files = 3);
void usb_unmount();
