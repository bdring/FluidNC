#pragma once

#include <OLEDDisplay.h>
#include "Machine/I2CBus.h"
#include <algorithm>

namespace Machine {
    class I2CBus;
}

class SSD1306_I2C : public OLEDDisplay {
private:
    uint8_t          _address;
    Machine::I2CBus* _i2c;
    uint32_t         _frequency;
    bool             _error = false;

public:
    SSD1306_I2C(uint8_t address, OLEDDISPLAY_GEOMETRY g, Machine::I2CBus* i2c, uint32_t frequency);
    bool connect();
    void display(void);

private:
    int getBufferOffset(void);

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
