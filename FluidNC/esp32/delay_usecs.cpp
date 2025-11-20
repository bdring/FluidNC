#include "Driver/delay_usecs.h"

#include <esp_attr.h>  // IRAM_ATTR
#include <xtensa/core-macros.h>

#include <sdkconfig.h>

// #include <esp_clk_tree.h>
// esp_clk_cpu_freq() was present in some versions of ESP-IDF but the version
// of framework-arduinoespressif32 that we are using does not have the associated
// libesp_hw_support.a library in the esp32s3 subdirectory.  It is present in the
// other subdirectories (esp32/, esp32s2/ and esp32c3)
int esp_clk_cpu_freq(void) {
    // This is how to do it with newer ESP-IDF versions
    // uint32_t cpu_freq_hz;
    // esp_clk_tree_src_get_freq_hz(SOC_CPU_CLK_SRC_XTAL, ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &cpu_freq_hz);
    // return cpu_freq_hz;

    return 240000000;
}

uint32_t ticks_per_us;

// cppcheck-suppress unusedFunction
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
