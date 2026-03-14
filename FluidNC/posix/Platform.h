#pragma once

#include "../src/Stepping.h"

// Use the Simulator stepping engine for POSIX builds
#define DEFAULT_STEPPING_ENGINE Stepping::SIMULATOR
#define MAX_N_SIMULATOR 1

// Maximum number of axes
#define MAX_N_AXIS 6

// Disable hardware-specific features
#define MAX_N_RMT 0
#define MAX_N_I2SO 0
