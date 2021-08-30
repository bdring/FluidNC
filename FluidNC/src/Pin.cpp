// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Pin.h"

// Pins:
#include "Logging.h"
#include "Pins/PinOptionsParser.h"
#include "Pins/GPIOPinDetail.h"
#include "Pins/VoidPinDetail.h"
#include "Pins/I2SOPinDetail.h"
#include "Pins/ErrorPinDetail.h"
#include <stdio.h>  // snprintf()

Pins::PinDetail* Pin::undefinedPin = new Pins::VoidPinDetail();
Pins::PinDetail* Pin::errorPin     = new Pins::ErrorPinDetail("unknown");

const char* Pin::parse(StringRange tmp, Pins::PinDetail*& pinImplementation) {
    String str = tmp.str();

    // Initialize pinImplementation first! Callers might want to delete it, and we don't want a random pointer.
    pinImplementation = nullptr;

    // Parse the definition: [GPIO].[pinNumber]:[attributes]

    // Skip whitespaces at the start
    auto nameStart = str.begin();
    for (; nameStart != str.end() && ::isspace(*nameStart); ++nameStart) {}

    if (nameStart == str.end()) {
        // Re-use undefined pins happens in 'create':
        pinImplementation = new Pins::VoidPinDetail();
        return nullptr;
    }

    auto idx = nameStart;
    for (; idx != str.end() && *idx != '.' && *idx != ':'; ++idx) {
        *idx = char(::tolower(*idx));
    }

    String prefix = str.substring(0, int(idx - str.begin()));

    if (idx != str.end()) {  // skip '.'
        ++idx;
    }

    int pinNumber = 0;
    if (prefix != "") {
        if (idx != str.end()) {
            for (int n = 0; idx != str.end() && n <= 4 && *idx >= '0' && *idx <= '9'; ++idx, ++n) {
                pinNumber = pinNumber * 10 + int(*idx - '0');
            }
        }
    }

    while (idx != str.end() && ::isspace(*idx)) {
        ++idx;
    }

    String options;
    if (idx != str.end()) {
        if (*idx != ':') {
            // Pin definition attributes or EOF expected.
            return "Pin attributes (':') were expected.";
        }
        ++idx;

        options = str.substring(int(idx - str.begin()));
    }

    // What would be a simple, practical way to parse the options? I figured, why not
    // just use the C-style string, convert it to lower case, and change the separators
    // into 'nul' tokens. We then pass the number of 'nul' tokens, and the first char*
    // which is pretty easy to parse.

    // Build an options parser:
    Pins::PinOptionsParser parser(options.begin(), options.end());

    log_debug("Attempting to set up pin: " << prefix << " index " << int(pinNumber));

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

Pin Pin::create(const String& str) {
    return create(StringRange(str));
}

Pin Pin::create(const StringRange& str) {
    Pins::PinDetail* pinImplementation = nullptr;
    try {
        log_debug("Setting up pin " << str.str());

        const char* err = parse(str, pinImplementation);
        if (err) {
            if (pinImplementation) {
                delete pinImplementation;
            }

            log_error("Setting up pin:" << str.str() << " failed:" << err);
            return Pin(new Pins::ErrorPinDetail(str.str()));
        } else {
            return Pin(pinImplementation);
        }
    } catch (const AssertionFailed& ex) {  // We shouldn't get here under normal circumstances.

        char buf[255];
        snprintf(buf, 255, "ERR: Setting up pin [%s] failed. Details: %s", str.str().c_str(), ex.what());

        Assert(false, buf);

        /*
          log_error("ERR: Setting up pin ["<<str.str()<<"] failed. Details:"<< ex.what());

        return Pin(new Pins::ErrorPinDetail(str.str()));
        */
    }
}

bool Pin::validate(const String& str) {
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
