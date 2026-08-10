/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// clang-format off
#include <esp_idf_version.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#if ESP_IDF_VERSION_MINOR == 1
#include "fnc_idf_uart_5_1.h"
#elif ESP_IDF_VERSION_MINOR == 3
#include "fnc_idf_uart_5_3.h"
#elif ESP_IDF_VERSION_MINOR == 5
#include "fnc_idf_uart_5_5_4.h"
#endif
#else
#include "fnc_idf_uart_4.h"
#endif
