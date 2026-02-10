// Copyright (c) 2026 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/backtrace.h"

bool backtrace_available(void) {
    return false;
}

bool backtrace_get(backtrace_t* bt) {
    return false;
}

void backtrace_clear(void) {}
