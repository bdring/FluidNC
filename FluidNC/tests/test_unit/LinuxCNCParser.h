#pragma once

// Extracted from LinuxCNC interp_read.cc for comparative testing
// Main entry point: linuxcnc_read_value(line, counter, result)

// Reads a value from line starting at counter position
// Returns 0 on success, negative on error
int linuxcnc_read_value(const char *line, int *counter, double *result);
