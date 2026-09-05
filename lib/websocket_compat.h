// Patched wrapper for mengrao websocket.h for macOS compatibility
#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>

// Fix type compatibility issue: size_t vs uint64_t mismatch on macOS
namespace std_compat {
    template <typename T1, typename T2>
    inline auto min(T1 a, T2 b) {
        // Cast both to uint64_t to avoid type mismatch
        return (uint64_t)a < (uint64_t)b ? (uint64_t)a : (uint64_t)b;
    }
}

// Inject our min function before the library includes std::min
namespace websocket {
    using std_compat::min;
}

// Now include the actual websocket library
#include "mengrao_websocket/websocket.h"
