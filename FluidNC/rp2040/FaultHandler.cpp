#include "hardware/exception.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

namespace {
    constexpr uint32_t kPanicMagic = 0x464E4350;  // "FNCP"
}

extern "C" __attribute__((noreturn)) void rp_hardfault_c_handler(uint32_t* stacked, uint32_t exc_return) {
    uint32_t pc   = stacked ? stacked[6] : 0;
    uint32_t lr   = stacked ? stacked[5] : 0;
    uint32_t xpsr = stacked ? stacked[7] : 0;

    watchdog_hw->scratch[0] = kPanicMagic;
    watchdog_hw->scratch[1] = pc;
    watchdog_hw->scratch[2] = lr;
    watchdog_hw->scratch[3] = xpsr ^ exc_return;

    watchdog_reboot(0, 0, 10);
    while (true) {
        tight_loop_contents();
    }
}

extern "C" __attribute__((naked, noreturn)) void rp_hardfault_handler() {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
    "b rp_hardfault_c_handler\n");
}

void install_fault_handlers() {
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, rp_hardfault_handler);
}
