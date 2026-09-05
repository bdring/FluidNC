// Copyright (c) 2026 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstddef>

// Size in bytes of the largest single allocation that would currently succeed.
// A fragmentation problem shows up here: total free heap can look healthy while
// the largest contiguous block shrinks and never recovers.
//
// Returns 0 on platforms with no API for it (rp2040, native host).  On those,
// only heapLowWater (total-free low-water) is meaningful.
size_t platform_max_free_block();
