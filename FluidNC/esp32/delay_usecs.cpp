#include "Driver/delay_usecs.h"

#include <esp_attr.h>  // IRAM_ATTR
#include <xtensa/core-macros.h>

#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32
#    include "esp32/clk.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#    include "esp32s2/clk.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#    include "esp32s3/clk.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#    include "esp32c3/clk.h"
#elif CONFIG_IDF_TARGET_ESP32H2
#    include "esp32h2/clk.h"
#endif

uint32_t ticks_per_us;
int      esp_clk_cpu_freq(void);

void timing_init() {
    ticks_per_us = esp_clk_cpu_freq() / 1000000;
}

void IRAM_ATTR delay_us(int32_t us) {
    spinUntil(usToEndTicks(us));
}

int32_t IRAM_ATTR usToCpuTicks(int32_t us) {
    return us * ticks_per_us;
}

int32_t IRAM_ATTR usToEndTicks(int32_t us) {
    return getCpuTicks() + usToCpuTicks(us);
}

// At the usual ESP32 clock rate of 240MHz, the range of this is
// just under 18 seconds, but it really should be used only for
// short delays up to a few tens of microseconds.

void IRAM_ATTR spinUntil(int32_t endTicks) {
    while ((getCpuTicks() - endTicks) < 0) {
#ifdef ESP32
        asm volatile("nop");
#endif
    }
}

// Short delays measured using the CPU cycle counter.  There is a ROM
// routine "esp_delay_us(us)" that almost does what what we need,
// except that it is in ROM and thus dodgy for use from ISRs.  We
// duplicate the esp_delay_us() here, but placed in IRAM, inlined,
// and factored so it can be used in different ways.

int32_t IRAM_ATTR getCpuTicks() {
    return XTHAL_GET_CCOUNT();
}
