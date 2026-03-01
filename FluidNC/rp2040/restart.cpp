#include "Driver/restart.h"
#include "Driver/backtrace.h"
#include "Logging.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include <cstdio>
#include <rp2040.h>

namespace {
    constexpr uint32_t kPanicMagic = 0x464E4350;  // "FNCP"

    bool       _panic_checked = false;
    bool       _panic_seen    = false;
    bool       _panic_logged  = false;
    backtrace_t _panic_bt     = {};

    void log_panic_context_once() {
        if (!_panic_seen || _panic_logged) {
            return;
        }

        _panic_logged = true;
        char pc_buf[16];
        char lr_buf[16];
        char state_buf[16];
        snprintf(pc_buf, sizeof(pc_buf), "0x%08x", _panic_bt.pc);
        snprintf(lr_buf, sizeof(lr_buf), "0x%08x", _panic_bt.excvaddr);
        snprintf(state_buf, sizeof(state_buf), "0x%08x", _panic_bt.exccause);
        log_error("Previous crash context (PC=" << pc_buf << " LR=" << lr_buf << " STATE=" << state_buf << ")");
    }

    void load_panic_info() {
        if (_panic_checked) {
            return;
        }

        _panic_checked = true;

        if (watchdog_hw->scratch[0] != kPanicMagic) {
            return;
        }

        _panic_seen          = true;
        _panic_bt.pc         = watchdog_hw->scratch[1];
        _panic_bt.excvaddr   = watchdog_hw->scratch[2];
        _panic_bt.exccause   = watchdog_hw->scratch[3];
        _panic_bt.num_addresses = (_panic_bt.pc != 0) ? 1 : 0;
        if (_panic_bt.num_addresses > 0) {
            _panic_bt.addresses[0] = _panic_bt.pc;
        }

        watchdog_hw->scratch[0] = 0;
        watchdog_hw->scratch[1] = 0;
        watchdog_hw->scratch[2] = 0;
        watchdog_hw->scratch[3] = 0;
    }
}
// Trigger a software reset
void restart() {
    watchdog_reboot(0, 0, 10);
    while (1) {}
}

bool restart_was_panic() {
    load_panic_info();
    log_panic_context_once();

    return _panic_seen;
}

extern "C" bool rp2040_get_last_panic_backtrace(backtrace_t* bt) {
    load_panic_info();
    log_panic_context_once();
    if (!_panic_seen || bt == nullptr) {
        return false;
    }
    *bt = _panic_bt;
    return true;
}

extern "C" void rp2040_clear_last_panic_backtrace() {
    _panic_seen = false;
    _panic_bt   = {};
}
