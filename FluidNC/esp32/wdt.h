#pragma once

void enable_core0_WDT();
void disable_core0_WDT();

// Switch the task watchdog to report-only with a long timeout.  Call early in
// startup, before anything slow runs on a watched task.
void configure_task_watchdog();
