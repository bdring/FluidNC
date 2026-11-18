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
#if defined(ETH_PHY_IP101)
        { EthPhy::IP101, "ip101" },
#endif
        EnumItem(EthPhy::W5500),
    };

    const EnumItem EthPhy::clockModes[] = {
        { EthPhy::ClockExtIn, "ext_in" },
        { EthPhy::ClockOut, "out" },
        EnumItem(EthPhy::ClockExtIn),
    };

    static eth_phy_type_t arduinoPhyType(uint32_t phy_type) {
        switch (phy_type) {
            case EthPhy::KSZ8851:
                return ETH_PHY_KSZ8851;
            case EthPhy::DM9051:
                return ETH_PHY_DM9051;
#if defined(ETH_PHY_IP101)
            case EthPhy::IP101:
                return ETH_PHY_IP101;
#endif
            case EthPhy::W5500:
            default:
                return ETH_PHY_W5500;
        }
    }

#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32)
    static eth_clock_mode_t arduinoClockMode(uint32_t mode) {
#    if defined(CONFIG_IDF_TARGET_ESP32P4)
        return mode == EthPhy::ClockOut ? EMAC_CLK_OUT : EMAC_CLK_EXT_IN;
#    else
        return mode == EthPhy::ClockOut ? ETH_CLOCK_GPIO0_OUT : ETH_CLOCK_GPIO0_IN;
#    endif
    }
#endif

    void EthPhy::validate() {
        if (isRMII()) {
            Assert(_mdc.defined() && _mdio.defined(), "Ethernet mdc_pin and mdio_pin must be configured");
            return;
        }
        // config->_spi->defined() cannot be checked here: SPIBus::defined()
        // only becomes true once SPIBus::init() actually runs the hardware
        // init, which happens later in the startup sequence than validate().
        // That check belongs in init(), same as SDCard does it.
        Assert(_cs.defined(), "Ethernet cs_pin must be configured");
    }

    void EthPhy::afterParse() {}

    bool EthPhy::init() {
        if (isRMII()) {
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32)
            if (!_mdc.defined() || !_mdio.defined()) {
                log_error("Ethernet RMII needs mdc_pin and mdio_pin");
                return false;
            }

            _mdc.setAttr(Pin::Attr::Output);
            _mdio.setAttr(Pin::Attr::Input);
            pinnum_t mdc  = _mdc.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            pinnum_t mdio = _mdio.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

            int power = -1;
            if (_rst.defined()) {
                _rst.setAttr(Pin::Attr::Output);
                power = _rst.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            }

            log_info("Ethernet RMII PHY " << phyTypes[_phy_type].name << " mdc:" << _mdc.name() << " mdio:" << _mdio.name()
                                          << " rst:" << _rst.name());

            bool ok = ETH.begin(arduinoPhyType(_phy_type), _phy_addr, int(mdc), int(mdio), power, arduinoClockMode(_clock_mode));
            if (!ok) {
                log_error("Ethernet RMII PHY init failed");
                return false;
            }
            config_ok = true;
            return true;
#else
            log_error("Ethernet RMII is not supported on this target");
            return false;
#endif
        }

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
                            int(misoPin),
                            int(mosiPin),
                            uint8_t(_frequency_hz / 1000000));
        if (!ok) {
            log_error("Ethernet PHY init failed");
            return false;
        }
        config_ok = true;
        return true;
    }
}
#endif
