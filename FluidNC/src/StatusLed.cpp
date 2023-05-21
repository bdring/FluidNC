#include "StatusLed.h"

StatusLed::StatusLed() : pixels(1, 45, NEO_GRB + NEO_KHZ800) {}

void StatusLed::init() {
    pixels.begin();
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(64, 0, 0));
    pixels.show();
}

// TODO FIXME: Added alarm = red, etc.

void StatusLed::update() {
    //if (Uart0.isConnected()) {
    pixels.setPixelColor(0, pixels.Color(0, 64, 64));
    //} else {
    //    pixels.setPixelColor(0, pixels.Color(0, 64, 0));
    //}
    pixels.show();
}

StatusLed statusLed;
