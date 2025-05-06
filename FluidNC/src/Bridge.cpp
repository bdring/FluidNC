// Copyright (c) 2024 - Bridge Mode Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/**
 * Bridge.cpp - Implementation of a USB-UART bridge mode for FluidNC
 * 
 * This module implements a "bridge mode" that allows direct communication between:
 * - The USB serial connection to the computer
 * - A UART connection to an external device (like a pendant)
 *
 * The bridge mode can be entered using the $BRIDGE command and exited using:
 * 1. $NORMAL or $$NORMAL command from USB (computer side)
 * 2. $$EXIT_BRIDGE command from the pendant
 * 3. Ctrl+C (ASCII 3) character from a terminal
 * 4. Automatic timeout after inactivity (5 min default, configurable)
 *
 * While in bridge mode, normal FluidNC operations are suspended.
 */

#include "Bridge.h"
#include "Machine/MachineConfig.h"
#include "Serial.h"  // allChannels
#include "Report.h"  // log functions
#include "Uart.h"
#include "UartChannel.h"
#include "System.h"
#include "Channel.h"

// Static member initializations
int         BridgeMode::_uart_num      = -1;
bool        BridgeMode::_active        = false;
int         BridgeMode::_timeout_ms    = 0;
TickType_t  BridgeMode::_last_activity = 0;

// Initialize bridge mode system
void BridgeMode::init() {
    _active = false;
    _uart_num = -1;
    _timeout_ms = 0;
}

// Start bridge mode with the specified UART
bool BridgeMode::start(int uart_num, int timeout_ms) {
    if (_active) {
        return false; // Already active
    }

    // Check if the UART is valid
    if (uart_num < 1 || uart_num >= MAX_N_UARTS || !config->_uarts[uart_num]) {
        log_error("Invalid UART number for bridge mode: " << uart_num);
        return false;
    }

    _uart_num = uart_num;
    _timeout_ms = timeout_ms;
    _active = true;
    _last_activity = xTaskGetTickCount();
    
    // Pause the UART channel if it exists to prevent it from processing commands
    UartChannel* channel = nullptr;
    for (size_t n = 0; (channel = config->_uart_channels[n]) != nullptr; ++n) {
        if (channel->uart_num() == _uart_num) {
            channel->pause();
            break;
        }
    }

    // Set the system state to bridge mode
    set_state(State::Bridge);
    
    return true;
}

// Stop bridge mode
void BridgeMode::stop() {
    if (!_active) {
        return;
    }

    _active = false;
    
    // Resume the UART channel if it was paused
    UartChannel* channel = nullptr;
    for (size_t n = 0; (channel = config->_uart_channels[n]) != nullptr; ++n) {
        if (channel->uart_num() == _uart_num) {
            channel->resume();
            break;
        }
    }

    // Reset to idle state
    set_state(State::Idle);
    
    _uart_num = -1;
}

// Process data in bridge mode
void BridgeMode::process() {
    if (!_active || _uart_num < 0) {
        return;
    }

    // Set a default timeout if one wasn't specified (5 minutes)
    static const uint32_t DEFAULT_MAX_INACTIVE_TIME = 300000; // 5 minutes in milliseconds
    
    // If no activity for more than the timeout, exit bridge mode
    if (_timeout_ms == 0 && (xTaskGetTickCount() - _last_activity) > (DEFAULT_MAX_INACTIVE_TIME / portTICK_PERIOD_MS)) {
        log_info("Bridge mode timed out after " << DEFAULT_MAX_INACTIVE_TIME / 1000 << "s of inactivity");
        stop();
        return;
    }
    
    // Normal timeout check
    if (check_timeout()) {
        return;
    }

    Uart* uart = config->_uarts[_uart_num];
    if (!uart) {
        log_error("UART not available for bridge mode");
        stop();
        return;
    }

    const int buflen = 256;
    uint8_t buffer[buflen];
    
    // Forward data from USB to UART
    size_t len;
    len = Uart0.available();
    if (len > 0) {
        if (len > buflen) {
            len = buflen;
        }
        
        for (size_t i = 0; i < len; i++) {
            int c = Uart0.read();
            if (c >= 0) {
                buffer[i] = c;
            } else {
                len = i;
                break;
            }
        }
        
        if (len > 0) {
            // Check for special command to exit bridge mode
            if ((len >= 9 && memcmp(buffer, "$$NORMAL\r\n", 9) == 0) || 
                (len >= 8 && memcmp(buffer, "$$NORMAL\n", 8) == 0) ||
                (len >= 7 && memcmp(buffer, "$NORMAL\n", 7) == 0) ||
                (len >= 8 && memcmp(buffer, "$NORMAL\r\n", 8) == 0) ||
                (len >= 6 && memcmp(buffer, "NORMAL", 6) == 0) ||
                (len >= 6 && memcmp(buffer, "normal", 6) == 0)) {
                Uart0.println("Exiting bridge mode");
                stop();
                return;
            }
            
            // Check for escape character (Ctrl+C = ASCII 3)
            for (size_t i = 0; i < len; i++) {
                if (buffer[i] == 3) { // Ctrl+C
                    Uart0.println("Detected Ctrl+C, exiting bridge mode");
                    stop();
                    return;
                }
            }
            
            uart->write(buffer, len);
            record_activity();
        }
    }
    
    // Forward data from UART to USB
    len = uart->available();
    if (len > 0) {
        if (len > buflen) {
            len = buflen;
        }
        
        for (size_t i = 0; i < len; i++) {
            int c = uart->read();
            if (c >= 0) {
                buffer[i] = c;
            } else {
                len = i;
                break;
            }
        }
        
        if (len > 0) {
            // Check if the pendant is requesting to exit bridge mode
            if ((len >= 12 && memcmp(buffer, "$$EXIT_BRIDGE", 12) == 0) || 
                (len >= 11 && memcmp(buffer, "$EXIT_BRIDGE", 11) == 0)) {
                Uart0.println("Pendant requested to exit bridge mode");
                stop();
                return;
            }
            
            Uart0.write(buffer, len);
            record_activity();
        }
    }
}

// Check if we should automatically exit bridge mode due to inactivity
bool BridgeMode::check_timeout() {
    if (!_active || _timeout_ms == 0) {
        return false;
    }
    
    if ((xTaskGetTickCount() - _last_activity) > (_timeout_ms / portTICK_PERIOD_MS)) {
        log_info("Bridge mode timed out after " << _timeout_ms << "ms of inactivity");
        stop();
        return true;
    }
    
    return false;
}

// Command handlers
Error cmd_bridge_start(const char* value, AuthenticationLevel auth_level, Channel& out) {
    int uart_num = 1; // Default to UART 1
    int timeout_ms = 0; // Default to no timeout (stay in bridge mode until explicit exit)

    if (value) {
        // Parse command parameters: $$BRIDGE=<uart_num>,<timeout_s>
        std::string_view rest(value);
        std::string_view first;
        bool first_param = true;

        while (string_util::split_prefix(rest, first, ',')) {
            if (first_param) {
                // First parameter is UART number
                if (!string_util::is_int(first, uart_num) || uart_num < 1 || uart_num >= MAX_N_UARTS) {
                    log_error_to(out, "Invalid UART number. Usage: $$BRIDGE=<uart_num>,<timeout_s>");
                    return Error::InvalidValue;
                }
                first_param = false;
            } else {
                // Second parameter is timeout in seconds
                int timeout_s;
                if (!string_util::is_int(first, timeout_s) || timeout_s < 0) {
                    log_error_to(out, "Invalid timeout value. Usage: $$BRIDGE=<uart_num>,<timeout_s>");
                    return Error::InvalidValue;
                }
                timeout_ms = timeout_s * 1000;
            }
        }
    }

    // Check if the UART exists
    if (!config->_uarts[uart_num]) {
        log_error_to(out, "UART" << uart_num << " does not exist");
        return Error::InvalidValue;
    }

    // Notify the pendant we're entering bridge mode
    if (config->_uart_channels[uart_num]) {
        config->_uart_channels[uart_num]->println("$$BRIDGE_START");
        delay_ms(100); // Give pendant time to prepare
    }

    bool result = BridgeMode::start(uart_num, timeout_ms);
    if (result) {
        log_info_to(out, "Entering bridge mode with UART" << uart_num);
        log_info_to(out, "Use $$NORMAL to exit bridge mode");
        
        if (timeout_ms > 0) {
            log_info_to(out, "Bridge will auto-exit after " << (timeout_ms / 1000) << "s of inactivity");
        }
    } else {
        log_error_to(out, "Failed to enter bridge mode");
        return Error::FailedToEnterBridgeMode;
    }

    return Error::Ok;
}

Error cmd_bridge_stop(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!BridgeMode::is_active()) {
        log_error_to(out, "Not in bridge mode");
        return Error::NotInBridgeMode;
    }

    // Notify the pendant we're exiting bridge mode
    int uart_num = BridgeMode::get_uart_num();
    if (uart_num > 0 && uart_num < MAX_N_UARTS && config->_uart_channels[uart_num]) {
        config->_uart_channels[uart_num]->println("$$BRIDGE_END");
        delay_ms(100); // Give pendant time to prepare
    }

    BridgeMode::stop();
    log_info_to(out, "Exited bridge mode");
    
    return Error::Ok;
} 