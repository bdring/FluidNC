#pragma once

#include <Adafruit_NeoPixel.h>

class StatusLed {
    Adafruit_NeoPixel pixels;

public:
    StatusLed();

    void init();
    void update();
};

extern StatusLed statusLed;
