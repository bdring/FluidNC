// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2023 -	Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Pin.h"

// Pins:
#include "Config.h"
#include "Pins/PinOptionsParser.h"
#include "Pins/GPIOPinDetail.h"
#include "Pins/VoidPinDetail.h"
#include "Pins/I2SOPinDetail.h"
#include "Pins/ChannelPinDetail.h"
#include "Pins/ErrorPinDetail.h"
#include "string_util.h"
#include "Machine/MachineConfig.h"  // config
#include <string_view>
#include <charconv>
#include "Pins/ExtPinDetail.h"

Pins::PinDetail* Pin::undefinedPin = new Pins::VoidPinDetail();
Pins::PinDetail* Pin::errorPin     = new Pins::ErrorPinDetail("unknown");

static constexpr bool verbose_debugging = false;

const char* Pin::parse(std::string_view pin_str, Pins::PinDetail*& pinImplementation) {
    if (verbose_debugging) {
        log_info("Parsing pin string: " << pin_str);
    }

    // Initialize pinImplementation first! Callers might want to delete it, and we don't want a random pointer.
    pinImplementation = nullptr;

    // Skip whitespaces at the start
    pin_str = string_util::trim(pin_str);

    if (pin_str.empty()) {
        // Reuse undefined pins happens in 'create':
        pinImplementation = undefinedPin;
        return nullptr;
    }

    std::string_view pin_type;
    string_util::split_prefix(pin_str, pin_type, '.');

    if (verbose_debugging) {
        log_info("Parsed pin type: " << pin_type << ", rest: " << pin_str);
    }

    std::string_view num_str;
    string_util::split_prefix(pin_str, num_str, ':');

    uint32_t pin_number;
    string_util::from_decimal(num_str, pin_number);

    if (verbose_debugging) {
        log_info("Parsed pin number: " << pin_number << ", options: " << pin_str);
    }

    // Build an options parser:
    Pins::PinOptionsParser parser(pin_str);

    // Build this pin:
    if (string_util::equal_ignore_case(pin_type, "gpio")) {
        pinImplementation = new Pins::GPIOPinDetail(static_cast<pinnum_t>(pin_number), parser);
        return nullptr;
    }
#if MAX_N_I2SO
    if (string_util::equal_ignore_case(pin_type, "i2so")) {
        pinImplementation = new Pins::I2SOPinDetail(static_cast<pinnum_t>(pin_number), parser);
        return nullptr;
    }
#endif
    if (string_util::starts_with_ignore_case(pin_type, "uart_channel")) {
        auto     num_str = pin_type.substr(strlen("uart_channel"));
        objnum_t channel_num;
        auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), channel_num);
        if (ec != std::errc() || ptr != (num_str.data() + num_str.size())) {
            return "Bad uart_channel number";
        }
        if (config->_uart_channels[channel_num] == nullptr) {
            return "uart_channel is not configured";
        }

        pinImplementation = new Pins::ChannelPinDetail(config->_uart_channels[channel_num], pin_number, parser);
        return nullptr;
    }

    if (string_util::equal_ignore_case(pin_type, "no_pin")) {
        pinImplementation = undefinedPin;
        return nullptr;
    }

    if (string_util::equal_ignore_case(pin_type, "void")) {
        // Note: having multiple void pins has its uses for debugging.
        pinImplementation = new Pins::VoidPinDetail();
        return nullptr;
    }

    if (string_util::starts_with_ignore_case(pin_type, "pinext")) {
        if (pin_type.length() == 7 && isdigit(pin_type[6])) {
            auto deviceId     = pin_type[6] - '0';
            pinImplementation = new Pins::ExtPinDetail(deviceId, pinnum_t(pin_number), parser);
        } else {
            // For now this should be sufficient, if not we can easily change it to 100 extenders:
            return "Incorrect pin extender specification. Expected 'pinext[0-9].[port number]'.";
        }
    }

    if (pinImplementation == nullptr) {
        log_error("Unknown pin type:" << pin_type);
        return "Unknown pin type";
    }
#ifdef DEBUG_PIN_DUMP
    pinImplementation = new Pins::DebugPinDetail(pinImplementation);
    return nullptr;
#else
    return "Unknown pin type";
#endif
}

Pin Pin::create(std::string_view str) {
    Pins::PinDetail* pinImplementation = nullptr;
    try {
        const char* err = parse(str, pinImplementation);
        if (err) {
            if (pinImplementation) {
                delete pinImplementation;
            }

            log_error("Setting up pin: " << str << " failed:" << err);
            return Pin(new Pins::ErrorPinDetail(str));
        } else {
            return Pin(pinImplementation);
        }
    } catch (std::exception& ex) {  // We shouldn't get here under normal circumstances.
        log_error(str << " - " << ex.what());
        Assert(false, "Pin creation failed");
        // return Pin(new Pins::ErrorPinDetail(str.str()));
    }
}

bool Pin::validate(const char* str) {
    Pins::PinDetail* pinImplementation;

    auto valid = parse(str, pinImplementation);
    if (pinImplementation) {
        delete pinImplementation;
    }

    return valid;
}

void Pin::report(const char* legend) {
    if (defined()) {
        log_info(legend << " " << name());
    }
}

Pin::~Pin() {
    if (_detail != undefinedPin && _detail != errorPin) {
        delete _detail;
    }
}
