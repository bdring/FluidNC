// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Wrapper to make std:error_code from ESP_IDF esp_err_t values

#include <esp_err.h>  // ESP_IDF error definitions
#include <system_error>

namespace esp_error {
    const std::error_category& category();
    inline std::error_code     make_error_code(esp_err_t err) { return std::error_code(err, esp_error::category()); }
}
