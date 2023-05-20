// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Pin.h"

// Pins:
#include "Config.h"
#include "Pins/PinOptionsParser.h"
#include "Pins/GPIOPinDetail.h"
#include "Pins/VoidPinDetail.h"
#include "Pins/I2SOPinDetail.h"
#include "Pins/ErrorPinDetail.h"
#include "Pins/ExtPinDetail.h"
#include <stdio.h>  // snprintf()

Pins::PinDetail* Pin::undefinedPin = new Pins::VoidPinDetail();
Pins::PinDetail* Pin::errorPin     = new Pins::ErrorPinDetail("unknown");

const char* Pin::parse(StringRange tmp, Pins::PinDetail*& pinImplementation) {
    // Initialize pinImplementation first! Callers might want to delete it, and we don't want a random pointer.
    pinImplementation = nullptr;

    // Parse the definition: [GPIO].[pinNumber]:[attributes]

    const char* end = tmp.end();

    // Skip whitespaces at the start
    auto nameStart = tmp.begin();
    for (; nameStart != end && ::isspace(*nameStart); ++nameStart) {}

    if (nameStart == end) {
        // Re-use undefined pins happens in 'create':
        pinImplementation = new Pins::VoidPinDetail();
        return nullptr;
    }

    auto idx = nameStart;
    for (; idx != end && *idx != '.' && *idx != ':'; ++idx) {}

    StringRange prefix(tmp.begin(), idx);

    if (idx != end) {  // skip '.'
        ++idx;
    }

    int pinNumber = 0;
    if (prefix != "") {
        if (idx != end) {
            for (int n = 0; idx != end && n <= 4 && *idx >= '0' && *idx <= '9'; ++idx, ++n) {
                pinNumber = pinNumber * 10 + int(*idx - '0');
            }
        }
    }

    while (idx != end && ::isspace(*idx)) {
        ++idx;
    }

    if (idx != end) {
        if (*idx != ':') {
            // Pin definition attributes or EOF expected.
            return "Pin attributes (':') were expected.";
        }
        ++idx;
    }

    // Build an options parser:
    Pins::PinOptionsParser parser(idx, end);

    // Build this pin:
    if (prefix == "gpio") {
        pinImplementation = new Pins::GPIOPinDetail(pinnum_t(pinNumber), parser);
    }
#ifdef ESP32
    if (prefix == "i2so") {
        pinImplementation = new Pins::I2SOPinDetail(pinnum_t(pinNumber), parser);
    }
#endif
    if (prefix == "no_pin") {
        pinImplementation = undefinedPin;
    }

    if (prefix == "void") {
        // Note: having multiple void pins has its uses for debugging.
        pinImplementation = new Pins::VoidPinDetail();
    }

    if (prefix.substr(0, 6) == "pinext") {
        if (prefix.length() == 7 && prefix[6] >= '0' && prefix[6] <= '9') {
            auto deviceId     = prefix[6] - '0';
            pinImplementation = new Pins::ExtPinDetail(deviceId, pinnum_t(pinNumber), parser);
        } else {
            // For now this should be sufficient, if not we can easily change it to 100 extenders:
            return "Incorrect pin extender specification. Expected 'pinext[0-9].[port number]'.";
        }
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

Pin Pin::create(const StringRange& str) {
    Pins::PinDetail* pinImplementation = nullptr;
    try {
        const char* err = parse(str, pinImplementation);
        if (err) {
            if (pinImplementation) {
                delete pinImplementation;
            }

            log_error("Setting up pin:" << str.str() << " failed:" << err);
            return Pin(new Pins::ErrorPinDetail(str.str().c_str()));
        } else {
            return Pin(pinImplementation);
        }
    } catch (const AssertionFailed& ex) {  // We shouldn't get here under normal circumstances.

        char buf[255];
        snprintf(buf, 255, "ERR: %s - %s", str.str().c_str(), ex.what());

        Assert(false, buf);

        /*
          log_error("ERR: " << str.str() << " - " << ex.what());

        return Pin(new Pins::ErrorPinDetail(str.str()));
        */
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
