#include "Driver/usbmsc.h"
#include "Config.h"
#include "FluidError.hpp"
#include <system_error>

bool usb_init_host() {
#if MAX_N_USB_HOST
    return true;
#else
    return false;
#endif
}

std::error_code usb_mount(uint32_t) {
#if MAX_N_USB_HOST
    return {};
#else
    return std::error_code(FluidError::SDNotConfigured, FluidErrorCategory::category());
#endif
}

void usb_unmount() {}
