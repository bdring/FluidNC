// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "EnumItem.h"
#include "Pin.h"

#include <cstdint>

namespace Machine {
    // Ethernet PHY types supported via Arduino-ESP32's SPI-attached
    // Ethernet driver (ETH.h). W5500 is the one we've actually tested,
    // but KSZ8851SNL and DM9051 use the same SPI-bus-plus-CS wiring and
    // the same ETH.begin() call shape with a different eth_phy_type_t,
    // so supporting them costs nothing extra here.
    //
    // RMII-attached PHYs (LAN8720, IP101, RTL8201, JL1101, KSZ8041, ...)
    // need a different pin set (MDC/MDIO/RMII clock, no SPI/CS/INT) and a
    // different ETH.begin() overload, so they are intentionally not
    // supported by this class. Add a separate config section for those
    // if/when they're needed.
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
            W5500    = 0,
            KSZ8851  = 1,
            DM9051   = 2,
        };
        static const EnumItem phyTypes[];

        uint32_t _phy_type = W5500;

        uint32_t _phy_addr = 1;

        // Matches SDCard's _frequency_hz naming/units (Hz, not MHz).
        uint32_t _frequency_hz = 20000000;

        bool config_ok = false;

        void group(Configuration::HandlerBase& handler) override {
            // @config cs_pin
            // @default NO_PIN
            // SPI chip-select pin for the Ethernet controller. The SPI bus itself
            // (sck/mosi/miso) is the machine's shared spi: section, same as SDCard.
            handler.item("cs_pin", _cs);

            // @config int_pin
            // @default NO_PIN
            // Optional interrupt pin. If not defined, the PHY is polled instead.
            handler.item("int_pin", _int);

            // @config rst_pin
            // @default NO_PIN
            // Optional hardware reset pin for the PHY.
            handler.item("rst_pin", _rst);

            // @config phy_type
            // @default W5500
            // Which SPI-attached Ethernet PHY chip is present. W5500 is the only one
            // actually tested; KSZ8851/DM9051 use the same SPI+CS wiring and ETH.begin()
            // call shape with a different chip-type constant, so they're offered too.
            // RMII-attached PHYs (LAN8720 and similar) are a different wiring/API shape
            // entirely and aren't supported by this section.
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
