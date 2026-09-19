void feed_watchdog();
void add_watchdog_to_task();

// Suspend/resume watchdog supervision of the calling task around an operation
// that is known to block for a long time and cannot be interrupted to feed the
// watchdog - an SD card initialisation, for instance, which sleeps inside SPI
// transactions.  Use in pairs, and only where a reboot would be worse than
// going briefly unsupervised.
void suspend_watchdog_for_task();
void resume_watchdog_for_task();
