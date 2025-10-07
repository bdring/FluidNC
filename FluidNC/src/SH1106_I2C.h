
#pragma once

#include <OLEDDisplay.h>
#include "Machine/I2CBus.h"
#include <algorithm>

using namespace Machine;

class SH1106_I2C : public OLEDDisplay {
private:
    uint8_t _address;
    I2CBus* _i2c;
    int     _frequency;
    bool    _error = false;

public:
    SH1106_I2C(uint8_t address, OLEDDISPLAY_GEOMETRY g, I2CBus* i2c, int frequency) :
        _address(address), _i2c(i2c), _frequency(frequency), _error(false) {
        setGeometry(g);
    }

    bool connect() {
#if 1
        if (this->_frequency != -1) {
            _i2c->_frequency = this->_frequency;
        }
#endif
        return true;
    }

    void display(void) {
        if (_error) {
            return;
        }
        const int x_offset = 2;  // SH1106 has 132 pixel RAM, display starts at column 2
#ifdef OLEDDISPLAY_DOUBLE_BUFFER
        uint8_t minBoundY = UINT8_MAX;
        uint8_t maxBoundY = 0;

        uint8_t minBoundX = UINT8_MAX;
        uint8_t maxBoundX = 0;
        uint8_t x, y;

        // Calculate the Y bounding box of changes
        // and copy buffer[pos] to buffer_back[pos];
        for (y = 0; y < (this->height() / 8); y++) {
            for (x = 0; x < this->width(); x++) {
                uint16_t pos = x + y * this->width();
                if (buffer[pos] != buffer_back[pos]) {
                    minBoundY = std::min(minBoundY, y);
                    maxBoundY = std::max(maxBoundY, y);
                    minBoundX = std::min(minBoundX, x);
                    maxBoundX = std::max(maxBoundX, x);
                }
                buffer_back[pos] = buffer[pos];
            }
            yield();
        }

        // If the minBoundY wasn't updated
        // we can savely assume that buffer_back[pos] == buffer[pos]
        // holdes true for all values of pos

        if (minBoundY == UINT8_MAX)
            return;

        // SH1106 doesn't support COLUMNADDR/PAGEADDR commands
        // We need to set page and column for each row
        for (y = minBoundY; y <= maxBoundY; y++) {
            uint8_t col_start = x_offset + minBoundX;
            sendCommand(0xB0 + y);                      // Set page address
            sendCommand(0x00 + (col_start & 0x0F));     // Set lower column address
            sendCommand(0x10 + ((col_start >> 4) & 0x0F)); // Set higher column address

            uint8_t* start = &buffer[(minBoundX + y * this->width()) - 1];
            uint8_t  save  = *start;

            *start = 0x40;  // control
            _i2c->write(_address, start, (maxBoundX - minBoundX) + 1 + 1);
            *start = save;
        }
#else
        // SH1106 doesn't support COLUMNADDR/PAGEADDR commands
        // We need to set page and column for each row
        uint8_t pages = (this->height() / 8);
        for (uint8_t page = 0; page < pages; page++) {
            sendCommand(0xB0 + page);                   // Set page address (0xB0-0xB7)
            sendCommand(0x00 + (x_offset & 0x0F));      // Set lower column address
            sendCommand(0x10 + ((x_offset >> 4) & 0x0F)); // Set higher column address

            buffer[-1] = 0x40;  // control byte for data
            _i2c->write(_address, (uint8_t*)&buffer[-1], this->width() + 1);
            buffer += this->width();
        }
        // Reset buffer pointer
        buffer -= pages * this->width();
#endif
    }

private:
    int getBufferOffset(void) { return 0; }

    inline void sendCommand(uint8_t command) __attribute__((always_inline)) {
        if (_error) {
            return;
        }
        uint8_t _data[2];
        _data[0] = 0x80;  // control
        _data[1] = command;
        if (_i2c->write(_address, _data, sizeof(_data)) < 0) {
            log_error("OLED is not responding");
            _error = true;
        }
    }
};
