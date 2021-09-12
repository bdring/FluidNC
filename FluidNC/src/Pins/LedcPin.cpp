// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    Ledc.cpp

    This is a driver for the ESP32 LEDC controller that is similar
    to the Arduino HAL driver for LEDC.  It differs from the Arduino
    driver by being able to handle output pin inversion in hardware,
    and by having the ledcSetDuty function in IRAM so it is safe
    to call it from ISRs.
*/
#include "LedcPin.h"

#include <soc/ledc_struct.h>
#include <driver/ledc.h>
#include <esp32-hal-ledc.h>    // ledcDetachPin
#include <esp32-hal-matrix.h>  // pinMatrixOutAttach
#include <esp32-hal-gpio.h>    // OUTPUT

extern "C" void __pinMode(pinnum_t pin, uint8_t mode);

static int ledcAllocateChannel() {
    static int nextLedcChannel = 0;

    // Increment by 2 because there are only 4 timers so only
    // four completely independent channels.  We could be
    // smarter about this and look for an unallocated channel
    // that is already on the same frequency.  There is some
    // code for that in PinUsers/PwmPin.cpp TryGrabChannel()

    Assert(nextLedcChannel < 8, "Out of LEDC PWM channels");
    nextLedcChannel += 2;
    return nextLedcChannel - 2;
}

int ledcInit(Pin& pin, int chan, double freq, uint8_t bit_num) {
    if (chan < 0) {
        // Allocate a channel
        chan = ledcAllocateChannel();
    }
    ledcSetup(chan, freq, bit_num);  // setup the channel

    auto nativePin = pin.getNative(Pin::Capabilities::PWM);

    // This is equivalent to ledcAttachPin with the addition of
    // using the hardware inversion function in the GPIO matrix.
    // We use that to apply the active low function in hardware.
    __pinMode(nativePin, OUTPUT);
    uint8_t function    = ((chan / 8) ? LEDC_LS_SIG_OUT0_IDX : LEDC_HS_SIG_OUT0_IDX) + (chan % 8);
    bool    isActiveLow = pin.getAttr().has(Pin::Attr::ActiveLow);
    pinMatrixOutAttach(nativePin, function, isActiveLow, false);
    return chan;
}

void IRAM_ATTR ledcSetDuty(uint8_t chan, uint32_t duty) {
    uint8_t g = chan >> 3, c = chan & 7;
    bool    on = duty != 0;
    // This is like ledcWrite, but it is called from an ISR
    // and ledcWrite uses RTOS features not compatible with ISRs
    // Also, ledcWrite infers enable from duty, which is incorrect
    // for use with RcServo which wants the
    LEDC.channel_group[g].channel[c].duty.duty        = duty << 4;
    LEDC.channel_group[g].channel[c].conf0.sig_out_en = on;
    LEDC.channel_group[g].channel[c].conf1.duty_start = on;
}
