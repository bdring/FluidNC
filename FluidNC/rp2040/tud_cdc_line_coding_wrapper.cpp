/*
 * Wrapper for tud_cdc_line_coding_cb
 * Provides custom handling for 1200 baud (BOOTSEL) and 1201 baud (watchdog reboot)
 * 
 * Build flag required: -Wl,--wrap=tud_cdc_line_coding_cb
 */

#include <tusb.h>
#include <pico/bootrom.h>
#include <pico/bootrom_constants.h>
#include <hardware/irq.h>
#include <hardware/resets.h>
#include <hardware/watchdog.h>
#include <pico/time.h>
#include <pico/runtime.h>
#include "hardware/gpio.h"

#ifdef __FREERTOS__
#    include <freertos/FreeRTOS.h>
#endif

// Forward declare the real function
extern "C" void __real_tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding);

// Wrapped function
extern "C" void __wrap_tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding) {
    if (!p_line_coding) {
        __real_tud_cdc_line_coding_cb(itf, p_line_coding);
        return;
    }

    uint32_t baud = p_line_coding->bit_rate;

    // 1200 baud: Reset to BOOTSEL (UF2) mode
    if (baud == 1200) {
        gpio_put(3, 1);  // Visual feedback: ON
#ifdef __FREERTOS__
        // Idle the other core if running FreeRTOS
        extern void __freertos_idle_other_core(void);
        __freertos_idle_other_core();
#endif
        // Disable NVIC IRQ to prevent further USB interrupts
        irq_set_enabled(USBCTRL_IRQ, false);

#if 0
        // Reset the entire USB hardware block
        reset_block(RESETS_RESET_USBCTRL_BITS);
        unreset_block(RESETS_RESET_USBCTRL_BITS);

        // Delay so the host detects the disconnect
        busy_wait_ms(10);  // Increased to 10ms for reliability
#endif

        // Visual feedback: OFF before BOOTSEL
        // gpio_put(3, 0);

        // Enable watchdog as a failsafe (2s)
        // watchdog_enable(2000, 1);

        // Enter BOOTSEL mode
        reset_usb_boot(0, 0);

        // If reset_usb_boot returns, turn GPIO ON and spin (should not happen)
        // gpio_put(3, 1);
        while (1) {
            // Watchdog will fire and reset the chip
        }
    }

    // 600 baud: Watchdog reboot (normal boot, not BOOTSEL)
    else if (baud == 600) {
        // Disable all interrupts to prevent interference
        uint32_t saved_irq = save_and_disable_interrupts();

        // Configure watchdog for a 10ms reboot
        // First, make sure watchdog is enabled
        watchdog_enable(10, true);

        // Tight spin loop - watchdog will fire and reset
        while (1) {
            __asm__ volatile("nop");
        }
    }

    // All other baud rates: call the original implementation
    else {
        __real_tud_cdc_line_coding_cb(itf, p_line_coding);
    }
}
