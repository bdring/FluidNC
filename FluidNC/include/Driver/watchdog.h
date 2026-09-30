#pragma once

void feed_watchdog();
void add_watchdog_to_task();

// Log any watchdog timeouts that have happened since the last call.  Called
// periodically from the polling task.
void report_watchdog_timeouts();

// Suspend/resume watchdog supervision of the calling task around an operation
// that is known to block for a long time and cannot be interrupted to feed the
// watchdog - an SD card initialisation, for instance, which sleeps inside SPI
// transactions.  Use in pairs, so the expected wait does not produce a
// spurious timeout report.
void suspend_watchdog_for_task();
void resume_watchdog_for_task();

// Scoped form of the pair above, so an early return or an exception cannot
// leave the task unsupervised.  Does not nest.
class WatchdogSuspend {
public:
    WatchdogSuspend() { suspend_watchdog_for_task(); }
    ~WatchdogSuspend() { resume_watchdog_for_task(); }
    WatchdogSuspend(const WatchdogSuspend&)            = delete;
    WatchdogSuspend& operator=(const WatchdogSuspend&) = delete;
};
