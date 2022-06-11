// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    Tiny OLED display code.

    This is for a minature 64x48 OLED display that is too small to
    display a lot of information at once.  Display items are shown
    mostly individually, formatted to be a readable as practical
    on the tiny display.
*/

#include "../Config.h"

#ifdef INCLUDE_OLED_TINY

#    include "oled_io.h"
#    include "../Main.h"  // display_init() and display()

void display_init() {
    // The following I2C address and GPIO numbers are correct
    // for a WeMOS D1 Mini 0.66 OLED Shield attached to an
    // ESP32 Mini board.
    //     Address          SDA          SCL
    init_oled(0x3c, GPIO_NUM_21, GPIO_NUM_22, GEOMETRY_64_48);

    oled->flipScreenVertically();

    oled->clear();
    oled->setLogBuffer(3, 10);

    oled->setTextAlignment(TEXT_ALIGN_LEFT);

    // The initial circle is a good indication of a recent reboot
    oled->fillCircle(32, 24, 10);

    oled->display();
}
static void oled_log_line(String line) {
    if (line.length()) {
        oled->clear();
        oled->setFont(ArialMT_Plain_10);
        oled->println(line);
        oled->drawLogBuffer(0, 0);
        oled->display();
    }
}
void oled_fill_rect(int x, int y, int w, int h) {
    oled->clear();
    oled->fillRect(x, y, w, h);
    oled->display();
}
static void oled_show_string(String s) {
    oled->clear();
    oled->setFont(ArialMT_Plain_16);
    oled->drawString(0, 0, s);
    oled->display();
}
static void oled_show_ip(String ip) {
    auto dotpos = ip.lastIndexOf('.');                       // Last .
    dotpos      = ip.substring(0, dotpos).lastIndexOf('.');  // Second to last .

    oled->clear();
    oled->setFont(ArialMT_Plain_16);
    oled->drawString(0, 0, ip.substring(0, dotpos));
    oled->drawString(0, 16, ip.substring(dotpos));
    oled->display();
}

// display() is the only entry point for runtime usage
void display(const char* tag, String s) {
    if (!strcmp(tag, "IP")) {
        oled_show_ip(s);
        return;
    }
    if (!strcmp(tag, "GCODE")) {
        oled_log_line(s);
        return;
    }
    if (!strcmp(tag, "TEXT")) {
        oled_show_string(s);
        return;
    }
}
#endif
