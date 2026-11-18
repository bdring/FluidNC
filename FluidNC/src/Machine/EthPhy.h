// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "EnumItem.h"
#include "Pin.h"

#include <cstdint>

namespace Machine {
    // Ethernet PHY types supported by Arduino-ESP32's ETH.h: SPI parts use
    // cs_pin; RMII parts use mdc_pin/mdio_pin and the fixed RMII pins.
    class EthPhy : public Configuration::Configurable {
    public:
        EthPhy() = default;

        // Chip select pin for the SPI-attached Ethernet controller.
        // The SPI bus itself (sck/mosi/miso) is the machine's shared
        // config->_spi, same as SDCard.
        Pin _cs;

        // Optional. If not defined, ETH.begin() polls instead of using
        // the interrupt pin.
        Pin _int;

        // Optional hardware reset pin.
        Pin _rst;

        // W5500 is 0; keep numeric values stable since they may end up in
        // saved config round-trips via the enum machinery.
        enum PhyType {
            W5500   = 0,
            KSZ8851 = 1,
            DM9051  = 2,
            IP101   = 3,
        };
        static const EnumItem phyTypes[];

        enum ClockMode {
            ClockExtIn = 0,  // 50 MHz REF_CLK driven by the PHY/oscillator
            ClockOut   = 1,  // ESP32-P4 drives the 50 MHz RMII clock
        };
        static const EnumItem clockModes[];

        uint32_t _phy_type = W5500;

        Pin _mdc;
        Pin _mdio;

        uint32_t _clock_mode = ClockExtIn;

        bool isRMII() const { return _phy_type == IP101; }

        uint32_t _phy_addr = 1;

        // Matches SDCard's _frequency_hz naming/units (Hz, not MHz).
        uint32_t _frequency_hz = 20000000;

        bool config_ok = false;

        void group(Configuration::HandlerBase& handler) override {
            // @config cs_pin
            // @default NO_PIN
            // @pin_attributes output
            // SPI chip-select pin for the Ethernet controller. The SPI bus itself
            // (sck/mosi/miso) is the machine's shared spi: section, same as SDCard.
            handler.item("cs_pin", _cs);

            // @config int_pin
            // @default NO_PIN
            // @pin_attributes input
            // Optional interrupt pin. If not defined, the PHY is polled instead.
            handler.item("int_pin", _int);

            // @config rst_pin
            // @default NO_PIN
            // @pin_attributes output
            // Optional hardware reset pin. For RMII PHYs it is passed to ETH.begin().
            handler.item("rst_pin", _rst);

            // @config mdc_pin
            // @default NO_PIN
            // @pin_attributes output
            // RMII management clock (MDC).
            handler.item("mdc_pin", _mdc);

            // @config mdio_pin
            // @default NO_PIN
            // @pin_attributes io
            // RMII management data (MDIO), bidirectional.
            handler.item("mdio_pin", _mdio);

            // @config clock_mode
            // @default ext_in
            // @ignore_drift YAML token is ext_in; the C++ field initializer is the ClockExtIn enum constant.
            // RMII REF_CLK direction: ext_in or out.
            handler.item("clock_mode", _clock_mode, clockModes);

            // @config phy_type
            // @default W5500
            // Ethernet PHY chip type. SPI types use cs_pin; IP101 uses RMII.
            handler.item("phy_type", _phy_type, phyTypes);

            // @config phy_addr
            // @default 1
            // SPI/management address of the PHY chip.
            handler.item("phy_addr", _phy_addr, 0, 31);

            // @config frequency_hz
            // @default 20000000
            // SPI clock speed used to talk to the Ethernet controller.
            handler.item("frequency_hz", _frequency_hz, 1000000, 80000000);
        }

        void validate() override;
        void afterParse() override;

        // Brings up the PHY via ETH.begin(). Returns true on success.
        bool init();

        ~EthPhy() = default;
    };
}
