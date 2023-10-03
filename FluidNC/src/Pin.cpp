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
#include "Pins/ErrorPinDetail.h"
#include "string_util.h"

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
        // Re-use undefined pins happens in 'create':
        pinImplementation = undefinedPin;
        return nullptr;
    }

    auto             dot_idx = pin_str.find('.');
    std::string_view prefix  = pin_str.substr(0, dot_idx);
    if (dot_idx != std::string::npos) {
        dot_idx += 1;
    }
    pin_str.remove_prefix(dot_idx);

    if (verbose_debugging) {
        log_info("Parsed pin prefix: " << prefix << ", rest: " << pin_str);
    }

    char*    pin_number_end = nullptr;
    uint32_t pin_number     = std::strtoul(pin_str.cbegin(), &pin_number_end, 10);
    pin_str.remove_prefix(pin_number_end - pin_str.cbegin());

    if (verbose_debugging) {
        log_info("Parsed pin number: " << pin_number << ", rest: " << pin_str);
    }

    if (!pin_str.empty() && pin_str[0] == ':') {
        pin_str.remove_prefix(1);
        if (pin_str.empty()) {
            return "Pin attributes after ':' were expected.";
        }
    }

    if (verbose_debugging) {
        log_info("Remaining pin options: " << pin_str);
    }

    // Build an options parser:
    Pins::PinOptionsParser parser(pin_str.cbegin(), pin_str.cend());

    // Build this pin:
    if (string_util::equal_ignore_case(prefix, "gpio")) {
        pinImplementation = new Pins::GPIOPinDetail(static_cast<pinnum_t>(pin_number), parser);
    }
    if (string_util::equal_ignore_case(prefix, "i2so")) {
        pinImplementation = new Pins::I2SOPinDetail(static_cast<pinnum_t>(pin_number), parser);
    }
    if (string_util::equal_ignore_case(prefix, "no_pin")) {
        pinImplementation = undefinedPin;
    }

    if (string_util::equal_ignore_case(prefix, "void")) {
        // Note: having multiple void pins has its uses for debugging.
        pinImplementation = new Pins::VoidPinDetail();
    }

    if (pinImplementation == nullptr) {
        log_error("Unknown prefix:" << prefix);
        return "Unknown pin prefix";
    } else {
#ifdef DEBUG_PIN_DUMP
        pinImplementation = new Pins::DebugPinDetail(pinImplementation);
#endif

        return nullptr;
    }
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
    } catch (const AssertionFailed& ex) {  // We shouldn't get here under normal circumstances.
        log_error("ERR: " << str << " - " << ex.what());
        Assert(false, "");
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

void IRAM_ATTR Pin::write(bool value) const {
    _detail->write(value);
}

void IRAM_ATTR Pin::synchronousWrite(bool value) const {
    _detail->synchronousWrite(value);
}

Pin::~Pin() {
    if (_detail != undefinedPin && _detail != errorPin) {
        delete _detail;
    }
}
