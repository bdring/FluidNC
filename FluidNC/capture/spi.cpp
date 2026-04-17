#include "Driver/spi.h"

#include "Stage1HostSupport.h"

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin, bool dma, int8_t sck_drive_strength, int8_t mosi_drive_strength) {
    ++Stage1HostSupport::g_spi.initCalls;
    Stage1HostSupport::g_spi.sck = sck_pin;
    Stage1HostSupport::g_spi.miso = miso_pin;
    Stage1HostSupport::g_spi.mosi = mosi_pin;
    Stage1HostSupport::g_spi.dma = dma;
    Stage1HostSupport::g_spi.sckDrive = sck_drive_strength;
    Stage1HostSupport::g_spi.mosiDrive = mosi_drive_strength;
    return Stage1HostSupport::g_spi.initResult;
}

void spi_deinit_bus() {
    ++Stage1HostSupport::g_spi.deinitCalls;
}
