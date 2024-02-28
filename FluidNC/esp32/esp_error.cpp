// Copyright (c) 202f Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Wrapper to make std:error_code from ESP_IDF esp_err_t values

#include "esp_error.hpp"  // Adapter

namespace esp_error {
    namespace detail {
        class category : public std::error_category {
        public:
            virtual const char* name() const noexcept override { return "esp_error"; }
            virtual std::string message(int value) const override {
                // Let the native function do the actual work
                return ::esp_err_to_name((esp_err_t)value);
            }
        };
    }  // namespace detail

    const std::error_category& category() {
        // The category singleton
        static detail::category instance;
        return instance;
    }
}  // namespace esp_error
