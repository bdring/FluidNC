#pragma once

// Include base capture platform configuration
#include "../capture/Platform.h"

// Override for POSIX simulator: enable simulator and use Simulator stepping engine
#undef MAX_N_SIMULATOR
#define MAX_N_SIMULATOR 1

#undef DEFAULT_STEPPING_ENGINE
#define DEFAULT_STEPPING_ENGINE Stepping::SIMULATOR
