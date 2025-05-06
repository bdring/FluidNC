// Copyright (c) 2024 - Bridge Mode Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Types.h"
#include "Config.h"
#include "Error.h"
#include "WebUI/Authentication.h"

// Forward declarations
class Channel;

// Bridge mode specific data
class BridgeMode {
private:
    static int         _uart_num;       // The UART number to bridge with USB
    static bool        _active;         // Flag indicating if bridge mode is active
    static int         _timeout_ms;     // Timeout in ms for inactivity before auto-exiting bridge mode (0 = disabled)
    static TickType_t  _last_activity;  // Last time data was transmitted in either direction

public:
    // Initialize bridge mode system
    static void init();

    // Check if bridge mode is active
    static bool is_active() { return _active; }
    
    // Get the current UART number
    static int get_uart_num() { return _uart_num; }

    // Start bridge mode with the specified UART
    static bool start(int uart_num, int timeout_ms = 0);

    // Stop bridge mode
    static void stop();

    // Process data in bridge mode (called from main loop)
    static void process();

    // Record activity to prevent timeout
    static void record_activity() { _last_activity = xTaskGetTickCount(); }

    // Handle timeout if needed
    static bool check_timeout();
};

// Command handlers - add extern to make them visible to other files
extern Error cmd_bridge_start(const char* value, AuthenticationLevel auth_level, Channel& out);
extern Error cmd_bridge_stop(const char* value, AuthenticationLevel auth_level, Channel& out); 