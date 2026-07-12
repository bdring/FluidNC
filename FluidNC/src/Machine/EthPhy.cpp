// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_ETH

#    include "EthPhy.h"
#    include "MachineConfig.h"
#    include "Assertion.h"

#    include <ETH.h>

namespace Machine {
    const EnumItem EthPhy::phyTypes[] = {
        { EthPhy::W5500, "w5500" },
        { EthPhy::KSZ8851, "ksz8851" },
        { EthPhy::DM9051, "dm9051" },
        EnumItem(EthPhy::W5500),
    };

    static eth_phy_type_t arduinoPhyType(uint32_t phy_type) {
        switch (phy_type) {
            case EthPhy::KSZ8851:
                return ETH_PHY_KSZ8851;
            case EthPhy::DM9051:
                return ETH_PHY_DM9051;
            case EthPhy::W5500:
            default:
                return ETH_PHY_W5500;
        }
    }

    void EthPhy::validate() {
        // config->_spi->defined() cannot be checked here: SPIBus::defined()
        // only becomes true once SPIBus::init() actually runs the hardware
        // init, which happens later in the startup sequence than validate().
        // That check belongs in init(), same as SDCard does it.
        Assert(_cs.defined(), "Ethernet cs_pin must be configured");
    }

    void EthPhy::afterParse() {}

    bool EthPhy::init() {
        if (!_cs.defined()) {
            log_debug("Ethernet not configured (no cs_pin)");
            return false;
        }
        if (!config->_spi->defined()) {
            log_error("Ethernet needs SPI defined");
            return false;
        }

        log_info("Ethernet PHY " << phyTypes[_phy_type].name << " cs_pin:" << _cs.name() << " int_pin:" << _int.name()
                                 << " rst_pin:" << _rst.name());

        _cs.setAttr(Pin::Attr::Output);
        pinnum_t csPin = _cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);

        int intPin = -1;
        if (_int.defined()) {
            _int.setAttr(Pin::Attr::Input);
            intPin = _int.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        }

        int rstPin = -1;
        if (_rst.defined()) {
            _rst.setAttr(Pin::Attr::Output);
            rstPin = _rst.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        }

        // config->_spi holds Pin objects for the shared SPI bus (sck/mosi/miso).
        // ETH.begin()'s SPI-Ethernet overload takes raw pin numbers and manages
        // its own ESP-IDF spi_master device on the host we give it; it does not
        // require config->_spi->init() to have been called first. We reuse the
        // same physical bus pins that SDCard and other SPI peripherals use, on
        // a separate SPI host (FSPI/SPI2) so it doesn't fight over the bus with
        // whatever host FluidNC's own spi_init_bus() uses for those.
        pinnum_t sckPin  = config->_spi->_sck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        pinnum_t mosiPin = config->_spi->_mosi.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        pinnum_t misoPin = config->_spi->_miso.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

        bool ok = ETH.begin(arduinoPhyType(_phy_type),
                            _phy_addr,
                            int(csPin),
                            intPin,
                            rstPin,
                            SPI2_HOST,
                            int(sckPin),
                            int(mosiPin),
                            int(misoPin),
                            uint8_t(_spi_freq_mhz));
        if (!ok) {
            log_error("Ethernet PHY init failed");
            return false;
        }
        config_ok = true;
        return true;
    }
}
#endif
