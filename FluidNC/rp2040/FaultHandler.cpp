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
        "movs r0, #4\n"
        "mov r1, lr\n"
        "tst r1, r0\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1:\n"
        "mrs r0, msp\n"
        "2:\n"
        "b rp_hardfault_c_handler\n");
}

void install_fault_handlers() {
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, rp_hardfault_handler);
}
