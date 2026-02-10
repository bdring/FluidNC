// Copyright (c) 2026 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BACKTRACE_MAX_ADDRESSES 30

typedef struct {
    uint32_t pc;                                   // Faulting program counter
    uint32_t excvaddr;                             // Exception virtual address
    uint32_t exccause;                             // Exception cause code
    uint32_t addresses[BACKTRACE_MAX_ADDRESSES];   // Backtrace PC addresses
    size_t   num_addresses;                        // Number of valid entries
} backtrace_t;

// Returns true if a saved backtrace from a previous panic is available
bool backtrace_available(void);

// Retrieves the saved backtrace. Returns false if no valid data.
bool backtrace_get(backtrace_t* bt);

// Clears the saved backtrace
void backtrace_clear(void);

#ifdef __cplusplus
}
#endif
