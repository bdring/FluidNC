// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "WebUI/Authentication.h"
#include "Pin.h"
#include "Error.h"

#include <cstdint>

class SDCard : public Configuration::Configurable {
public:
    enum class State : uint8_t {
        Idle          = 0,
        NotPresent    = 1,
        Busy          = 2,
        BusyUploading = 3,
        BusyParsing   = 4,
        BusyWriting   = 5,
        BusyReading   = 6,
    };

private:
    State _state;
    Pin   _cardDetect;
    Pin   _cs;

    uint32_t _frequency_hz = 8000000;  // Set to nonzero to override the default

public:
    SDCard();
    SDCard(const SDCard&)            = delete;
    SDCard& operator=(const SDCard&) = delete;

    void afterParse() override;

    const char* filename();
    bool        config_ok = false;

    // Initializes pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override {
        // @config cs_pin
        // @default NO_PIN
        // @pin_attributes output
        // SPI chip-select pin for the SD card. Must be a native MCU pin with output
        // capability. Required (non-NO_PIN) for the SD card to function -- an spi: section
        // must also be configured.
        handler.item("cs_pin", _cs);

        // @config card_detect_pin
        // @default NO_PIN
        // @pin_attributes input
        // Optional card-detect switch input. Purely informational -- shown in the startup
        // log, with no other feature attached to it.
        handler.item("card_detect_pin", _cardDetect);

        // @config frequency_hz
        // @default 8000000
        // @tuning typical
        // SPI clock speed used for the SD card. Try a lower value if the card has
        // consistent read/write problems.
        handler.item("frequency_hz", _frequency_hz, 400000, 20000000);
    }

    ~SDCard();
};
