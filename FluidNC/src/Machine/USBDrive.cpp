// Copyright (c) 2026
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "USBDrive.h"
#include "Driver/usbmsc.h"
#include "Report.h"

namespace Machine {
    void USBDrive::afterParse() {
        // No additional defaults required.
    }

    void USBDrive::group(Configuration::HandlerBase& handler) {
        handler.item("enabled", _enabled);
    }

    void USBDrive::init() {
        if (!_enabled) {
            config_ok = false;
            return;
        }

        config_ok = usb_init_host();
        if (!config_ok) {
            log_error("USB pendrive support is unavailable");
        }
    }
}
