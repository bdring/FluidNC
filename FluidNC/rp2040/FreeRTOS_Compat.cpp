// FreeRTOS compatibility for RP2040
// Provides implementations of FreeRTOS functions needed by FluidNC
// Note: The Earle Philhower framework should provide these, but they may not be 
// linked in depending on build configuration. This ensures they're available.

#include <freertos/FreeRTOS.h>
#include <cstddef>
#include <cstdint>
#include "Driver/backtrace.h"

extern "C" bool rp2040_get_last_panic_backtrace(backtrace_t* bt);
extern "C" void rp2040_clear_last_panic_backtrace();

// Linker-provided symbol marking the end of BSS section (start of heap)
extern char __bss_end__;

// Get the current stack pointer
// The stack pointer is stored in the sp register (r13 on ARM Cortex-M)
static inline void* get_current_sp() {
    register void* sp asm("sp");
    return sp;
}

// RP2040 has 264KB of SRAM total
// Starting from SRAM base at 0x20000000, ending at 0x20042000
extern "C" size_t xPortGetFreeHeapSize() {
    // Get the current stack pointer
    void* current_sp = get_current_sp();
    
    // Get the end of BSS section (start of heap)
    uintptr_t heap_start = reinterpret_cast<uintptr_t>(&__bss_end__);
    
    // Get current stack pointer position
    uintptr_t stack_top = reinterpret_cast<uintptr_t>(current_sp);
    
    // Calculate free heap: space between heap start and current stack top
    // This represents the available memory for heap before it would collide with the stack
    if (stack_top > heap_start) {
        return stack_top - heap_start;
    }
    
    // If stack pointer is not above heap, something is wrong; return 0 or a safe value
    return 0;
}

extern "C" bool backtrace_available(void) {
    backtrace_t bt;
    return rp2040_get_last_panic_backtrace(&bt);
}

extern "C" bool backtrace_get(backtrace_t* bt) {
    return rp2040_get_last_panic_backtrace(bt);
}

extern "C" void backtrace_clear(void) {
    rp2040_clear_last_panic_backtrace();
}
