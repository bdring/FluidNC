#pragma once

#include <SSD1306Wire.h>
extern SSD1306Wire* oled;

// The SDA and SCL pins must be ordinary GPIOs; mappings to Pin objects do not
// work because the Arduino Wire library performs GPIO setup operations that cannot
// be overridden.
void init_oled(uint8_t address, pinnum_t sda_gpio, pinnum_t scl_gpio, OLEDDISPLAY_GEOMETRY geometry);
