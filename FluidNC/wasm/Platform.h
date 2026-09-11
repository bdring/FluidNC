#pragma once

// Include base capture platform configuration (shared with macos/linux/windows_x86).
#include "../capture/Platform.h"

// Like posix/Platform.h: enable the Simulator stepping engine and make it
// the default, since browser-simulated stepping is the entire point of
// this port -- there is no real GPIO to pulse.
#undef MAX_N_SIMULATOR
#define MAX_N_SIMULATOR 1

#undef DEFAULT_STEPPING_ENGINE
#define DEFAULT_STEPPING_ENGINE Stepping::SIMULATOR
