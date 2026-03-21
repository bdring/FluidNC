# PosixAsyncTCP — Async TCP/IP for POSIX Systems

A POSIX-based socket wrapper that provides an ESP32 AsyncTCP-compatible API for Linux, macOS, and other POSIX systems. Enables FluidNC's network stack (WebUI, OTA, API) to work seamlessly in development and testing environments without ESP32-specific dependencies.

## Features

✅ **AsyncTCP API Compatibility** — Drop-in replacement for ESP32 AsyncTCP  
✅ **Non-blocking Operations** — Async connect, send, receive  
✅ **Multi-client Support** — Handle multiple connections simultaneously  
✅ **Connection State Machine** — Full TCP state tracking  
✅ **Timeout Management** — Track RX idleness and ACK timeouts  
✅ **Thread-safe** — All operations protected with mutexes  
✅ **Simple Integration** — Minimal changes to existing FluidNC code  

## Quick Start

### Build

```bash
cd FluidNC/posix/PosixAsyncTCP
cmake -B build
cd build
cmake --build .
```

### Use in FluidNC

```cpp
#include "PosixAsyncTCP.h"

// Create server
auto server = std::make_shared<AsyncServer>(IPADDR_ANY, 8080);

auto on_connect = [](void* arg, AsyncClient* client) {
    printf("Client connected: %s:%d\n",
           client->remoteIP().toString().c_str(),
           client->remotePort());
           
    auto on_data = [](void* arg, AsyncClient* c, uint8_t* data, size_t len) {
        printf("Received %d bytes\n", len);
    };
    
    client->onData(on_data, nullptr);
};

server->onClient(on_connect, nullptr);
server->begin();

// Start the async manager
PosixAsyncTCPManager::getInstance().begin();

// Server is ready to accept connections
```

## Architecture

### Components

- **AsyncClient** — TCP connection endpoint
- **AsyncServer** — TCP listener
- **PosixAsyncTCPManager** — Polling thread with select/epoll

### Threading

```
Main Thread (Protocol)          Polling Thread (AsyncTCP)
    |                                      |
    +-- Protocol loop                      +-- select() loop (125ms)
    |   (G-code execution, etc.)           |   (socket monitoring)
    |                                      |
    +-- WebUI requests                     +-- Accepts connections
    |   (read-only queries)                |   Delivers data to callbacks
    |                                      |
    +-- GCode output                       +-- Sends buffered data
        (sent to channels)                     (async to clients)
    
    Data flows via callback functions
    No blocking between threads (thread-safe queues used if needed)
```

## File Structure

```
FluidNC/posix/PosixAsyncTCP/
├── PosixAsyncTCP.h           # Header file (classes, API)
├── PosixAsyncTCP.cpp         # Implementation
├── PosixAsyncTCPTest.cpp     # Unit tests (Google Test)
├── CMakeLists.txt            # Build configuration
├── README.md                 # This file
└── INTEGRATION.md            # Detailed integration guide
```

## API Overview

### Server

```cpp
AsyncServer(uint16_t port);
AsyncServer(IPAddress addr, uint16_t port);

void begin();                              // Start listening
void end();                                // Stop listening
uint8_t status() const;                    // Get state (LISTEN/CLOSED)
void onClient(AcConnectHandler, void*);   // Set accept callback
```

### Client

```cpp
// Connection
bool connect(IPAddress ip, uint16_t port);
bool connect(const char* host, uint16_t port);
void close(bool now = false);

// State
bool connected() const;
bool connecting() const;
bool disconnecting() const;
bool disconnected() const;

// Data Transfer
size_t write(const char* data, size_t size);
size_t add(const char* data, size_t size);
bool send();
size_t ack(size_t len);

// Configuration
void setRxTimeout(uint32_t sec);
void setAckTimeout(uint32_t ms);
void setNoDelay(bool enable);

// Information
IPAddress remoteIP() const;
uint16_t remotePort() const;
IPAddress localIP() const;
uint16_t localPort() const;

// Callbacks
void onConnect(AcConnectHandler, void*);
void onDisconnect(AcConnectHandler, void*);
void onData(AcDataHandler, void*);
void onError(AcErrorHandler, void*);
void onAck(AcAckHandler, void*);
void onTimeout(AcTimeoutHandler, void*);
void onPoll(AcPollHandler, void*);
```

### Manager

```cpp
PosixAsyncTCPManager& getInstance();
void begin();
void end();
```

## Differences from ESP32 AsyncTCP

| Aspect | ESP32 AsyncTCP | PosixAsyncTCP |
|--------|---|---|
| Socket Impl | LWIP | POSIX (BSD sockets) |
| Threading | FreeRTOS tasks | std::thread |
| Flow Control | TCP window + backlog | EAGAIN/EWOULDBLOCK |
| Startup | Automatic in ESP-IDF | Manual `begin()` |
| Memory | Fixed heap | Dynamic allocation |
| Polling | 2ms (FreeRTOS tick) | 125ms (select timeout) |

## Testing

### Run Unit Tests

```bash
cd build
ctest --output-on-failure
```

Tests include:
- Client creation and connection
- Server listening and accepting
- Bidirectional data transfer
- Multiple simultaneous clients
- Timeout and error handling
- State machine transitions

### Manual Testing

```bash
# Terminal 1: Start WebUI
fluidnc --config config.yaml

# Terminal 2: Connect and send commands
curl http://localhost:8080/
curl -X POST http://localhost:8080/api/command -d '{"cmd":"$"}'

# Or use WebSocket
wscat -c ws://localhost:8080/ws
> $  # Sends Grbl command
```

## Integration with FluidNC

### Step 1: Build Configuration

Add to your platform CMakeLists.txt:

```cmake
if(POSIX_BUILD)
    add_subdirectory("FluidNC/posix/PosixAsyncTCP")
    target_link_libraries(FluidNC PosixAsyncTCP)
endif()
```

### Step 2: Conditional Headers

In `src/WebUI/WebUIServer.cpp`:

```cpp
#ifdef __APPLE__
    #include "posix/PosixAsyncTCP/PosixAsyncTCP.h"
#elif __linux__
    #include "posix/PosixAsyncTCP/PosixAsyncTCP.h"
#else
    #include <ESPAsyncTCP.h>
#endif
```

### Step 3: Startup/Shutdown

In `src/Protocol.cpp`:

```cpp
void Protocol::setup() {
    // Existing setup...
    
    #if defined(__APPLE__) || defined(__linux__)
        PosixAsyncTCPManager::getInstance().begin();
    #endif
}

void Protocol::end() {
    #if defined(__APPLE__) || defined(__linux__)
        PosixAsyncTCPManager::getInstance().end();
    #endif
    
    // Existing cleanup...
}
```

## Performance Notes

1. **Polling Interval** (125ms) — Tunable in source; lower = more responsive but higher CPU
2. **Thread Priority** — Polling thread runs at default priority; see INTEGRATION.md for tuning
3. **No Real-time Guarantees** — Suitable for development/testing, not production CNC
4. **Scalability** — Tested with 10+ concurrent connections on development hardware

## Known Limitations

1. **No SSL/TLS** — Plain TCP only (can add OpenSSL if needed)
2. **No UDP** — Only TCP sockets (IPv4)
3. **No IPv6** — Currently IPv4 only
4. **Platform Specific** — Tested on macOS and Linux; Windows WSL should work
5. **Signal Handling** — select() interrupted by signals; use non-blocking in callbacks

## Building on Different Systems

### macOS

```bash
brew install cmake
cmake -B build
cmake --build build
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake build-essential
cmake -B build
cmake --build build
```

### Windows (WSL2)

```bash
# In WSL2
sudo apt-get install cmake build-essential
cmake -B build
cmake --build build
```

## Debugging

### Enable Verbose Output

Uncomment debug logging in `PosixAsyncTCP.cpp`:

```cpp
// Uncomment for debug output
// #define DEBUG_POSIX_ASYNC 1

#ifdef DEBUG_POSIX_ASYNC
    #define LOG_DEBUG(fmt, ...) printf("[AsyncTCP] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) do {} while(0)
#endif
```

### Common Issues

**Port already in use:**
```bash
lsof -i :<port>  # Find process using port
kill -9 <pid>    # Kill it (if safe)
```

**Address already in use (TIME_WAIT):**
```bash
# Wait ~30 seconds or setsockopt(SO_REUSEADDR) in code
# (Already enabled in implementation)
```

**Client connections fail:**
- Verify server is listening: `netstat -an | grep :<port>`
- Check firewall: `sudo pfctl -s nat` (macOS)
- Enable debug logging to see socket errors

## Contributing

To extend PosixAsyncTCP:

1. Follow the existing callback pattern
2. Add unit tests for new features
3. Update INTEGRATION.md with usage examples
4. Ensure thread safety (use `std::lock_guard` for shared state)
5. Keep API compatible with ESP32 AsyncTCP

## See Also

- [INTEGRATION.md](INTEGRATION.md) — Detailed integration guide
- [PosixAsyncTCP.h](PosixAsyncTCP.h) — Full API documentation
- [FluidNC WebUI](../../WebUI/) — Main integration point
- [POSIX Sockets](https://man7.org/linux/man-pages/man7/socket.7.html) — System reference

## License

Same as FluidNC (GPL v3). See [LICENSE](../../../LICENSE).

---

**Questions?** Check INTEGRATION.md or open an issue on GitHub.
