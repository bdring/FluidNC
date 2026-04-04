#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Driver/i2s_out.h"
#include "Pin.h"

namespace Stage1HostSupport {
    struct I2CState {
        bool                 initError = false;
        int                  initCalls = 0;
        objnum_t             initBus = 0;
        pinnum_t             initSda = INVALID_PINNUM;
        pinnum_t             initScl = INVALID_PINNUM;
        uint32_t             initFrequency = 0;
        int                  writeResult = 0;
        int                  readResult = 0;
        objnum_t             lastWriteBus = 0;
        uint8_t              lastWriteAddress = 0;
        std::vector<uint8_t> lastWriteData;
        objnum_t             lastReadBus = 0;
        uint8_t              lastReadAddress = 0;
        std::vector<uint8_t> readData;
    };

    struct SPIState {
        bool    initResult = true;
        int     initCalls = 0;
        int     deinitCalls = 0;
        pinnum_t sck = INVALID_PINNUM;
        pinnum_t miso = INVALID_PINNUM;
        pinnum_t mosi = INVALID_PINNUM;
        bool     dma = false;
        int8_t   sckDrive = -1;
        int8_t   mosiDrive = -1;
    };

    struct I2SOState {
        bool           called = false;
        i2s_out_init_t params {};
    };

    extern I2CState g_i2c;
    extern SPIState g_spi;
    extern I2SOState g_i2so;

    void resetBusState();
    void resetWebUiState();
}
