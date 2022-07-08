// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  PWM capabilities provided by the ESP32 LEDC controller via the ESP-IDF driver
*/

#include "Driver/PwmPin.h"
#include "src/Logging.h"

#include <soc/ledc_struct.h>  // LEDC

#include "soc/soc_caps.h"
#include "driver/ledc.h"

//Use XTAL clock if possible to avoid timer frequency error when setting APB clock < 80 Mhz
//Need to be fixed in ESP-IDF
#ifdef SOC_LEDC_SUPPORT_XTAL_CLOCK
#    define LEDC_DEFAULT_CLK LEDC_USE_XTAL_CLK
#else
#    define LEDC_DEFAULT_CLK LEDC_AUTO_CLK
#endif

#if CONFIG_IDF_TARGET_ESP32  // ESP32/PICO-D4
#    include "esp32/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#    include "esp32s2/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#    include "esp32s3/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#    include "esp32c3/rom/gpio.h"
#else
#    error Target CONFIG_IDF_TARGET is not supported
#endif

static int allocateChannel() {
    static int nextLedcChannel = 0;

    // Increment by 2 because there are only 4 timers so only
    // four completely independent channels.  We could be
    // smarter about this and look for an unallocated channel
    // that is already on the same frequency.  There is some
    // code for that in PinUsers/PwmPin.cpp TryGrabChannel()

    Assert(nextLedcChannel < 8, "Out of LEDC PwmPin channels");
    nextLedcChannel += 2;
    return nextLedcChannel - 2;
}

// Calculate the highest PwmPin precision in bits for the desired frequency
// 80,000,000 (APB Clock) = freq * maxCount
// maxCount is a power of two between 2^1 and 2^20
// frequency is at most 80,000,000 / 2^1 = 40,000,000, limited elsewhere
// to 20,000,000 to give a period of at least 2^2 = 4 levels of control.
static uint8_t calc_pwm_precision(uint32_t frequency) {
    if (frequency == 0) {
        frequency = 1;  // Limited elsewhere but just to be safe...
    }

    // Increase the precision (bits) until it exceeds the frequency
    // The hardware maximum precision is 20 bits
    const uint8_t  ledcMaxBits = 20;
    const uint32_t apbFreq     = 80000000;
    const uint32_t maxCount    = apbFreq / frequency;
    for (uint8_t bits = 2; bits <= ledcMaxBits; ++bits) {
        if ((1u << bits) > maxCount) {
            return bits - 1;
        }
    }

    return ledcMaxBits;
}

PwmPin::PwmPin(Pin& pin, uint32_t frequency) : _frequency(frequency) {
    uint8_t bits  = calc_pwm_precision(frequency);
    _period       = (1 << bits) - 1;
    _channel      = allocateChannel();
    uint8_t group = (_channel / 8), timer = ((_channel / 2) % 4);

    ledc_timer_config_t ledc_timer = { .speed_mode      = ledc_mode_t(group),
                                       .duty_resolution = ledc_timer_bit_t(bits),
                                       .timer_num       = ledc_timer_t(timer),
                                       .freq_hz         = frequency,
                                       .clk_cfg         = LEDC_DEFAULT_CLK };

    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        log_error("ledc timer setup failed!");
        throw -1;
    }

    // The Arduino framework should do this, but neglects to
    ledc_bind_channel_timer(LEDC_HIGH_SPEED_MODE, ledc_channel_t(_channel), ledc_timer_t(_channel / 2));

    _gpio = pin.getNative(Pin::Capabilities::PWM);

    bool isActiveLow = pin.getAttr().has(Pin::Attr::ActiveLow);

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << _gpio),       /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
        .mode         = GPIO_MODE_OUTPUT,      /*!< GPIO mode: set input/output mode                     */
        .pull_up_en   = GPIO_PULLUP_DISABLE,   /*!< GPIO pull-up                                         */
        .pull_down_en = GPIO_PULLDOWN_DISABLE, /*!< GPIO pull-down                                       */
        .intr_type    = GPIO_INTR_DISABLE      /*!< GPIO interrupt type                                  */
    };
    gpio_config(&conf);

    // This is equivalent to ledcAttachPin with the addition of
    // using the hardware inversion function in the GPIO matrix.
    // We use that to apply the active low function in hardware.

    uint8_t function = ((_channel / 8) ? LEDC_LS_SIG_OUT0_IDX : LEDC_HS_SIG_OUT0_IDX) + (_channel % 8);
    gpio_matrix_out(_gpio, function, isActiveLow, false);
}

void IRAM_ATTR PwmPin::setDuty(uint32_t duty) {
    uint8_t g = _channel >> 3, c = _channel & 7;
    bool    on = duty != 0;

    // This is like ledcWrite, but it is called from an ISR
    // and ledcWrite uses RTOS features not compatible with ISRs
    // Also, ledcWrite infers enable from duty, which is incorrect
    // for use with RcServo which wants the

    // We might be able to use ledc_duty_config() which is IRAM_ATTR

    LEDC.channel_group[g].channel[c].duty.duty        = duty << 4;
    LEDC.channel_group[g].channel[c].conf0.sig_out_en = on;
    LEDC.channel_group[g].channel[c].conf1.duty_start = on;
}

PwmPin::~PwmPin() {
    // XXX we need to release the channel so it can be reused by later calls to pwmInit
#define MATRIX_DETACH_OUT_SIG 0x100
    gpio_matrix_out(_gpio, MATRIX_DETACH_OUT_SIG, false, false);
}
