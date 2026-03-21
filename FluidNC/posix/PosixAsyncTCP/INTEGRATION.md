# PosixAsyncTCP Integration Guide

## Overview

PosixAsyncTCP is a POSIX-compliant socket abstraction that provides an ESP32 AsyncTCP-like API for Linux/macOS development and testing environments. It enables POSIX TCP networking without requiring special platform-specific code, making FluidNC's network stack more testable and portable.

## Architecture

### Components

1. **AsyncClient** — Represents a TCP connection to a remote server
   - Non-blocking socket operations
   - Async connect, send, receive
   - Connection state tracking
   - Timeout handling

2. **AsyncServer** — Listens for incoming TCP connections
   - Accepts client connections
   - Delivers accepted clients via callback
   - Supports multiple simultaneous clients

3. **PosixAsyncTCPManager** — Central polling thread
   - Manages all registered servers and clients
   - Uses `select()` for efficient I/O multiplexing
   - Runs in a separate thread to avoid blocking the main protocol loop

### Threading Model

- **Polling Thread**: A dedicated thread runs the select/poll loop (125ms interval)
- **Main Thread**: FluidNC's Protocol/GCode execution continues unblocked
- **Callbacks**: Executed in polling thread context—must be non-blocking

## Usage in FluidNC

### Build Configuration

Add PosixAsyncTCP to your CMakeLists.txt:

```cmake
# In FluidNC/CMakeLists.txt or platform-specific config
if(POSIX_BUILD)
    add_subdirectory("FluidNC/posix/PosixAsyncTCP")
    target_link_libraries(FluidNC PUBLIC PosixAsyncTCP)
endif()
```

### Include Headers

In files that use async networking (e.g., WebUI/WebUIServer.cpp):

```cpp
#if defined(__linux__) || defined(__APPLE__)
    #include "PosixAsyncTCP.h"
#else
    #include <ESPAsyncTCP.h>
#endif
```

### Creating a Server

```cpp
// In WebUIServer.cpp or equivalent
auto server = std::make_shared<AsyncServer>(IPADDR_ANY, 8080);

auto accept_cb = [](void* arg, AsyncClient* client) {
    auto* handler = (MyHandler*)arg;
    handler->handleNewClient(client);
};

server->onClient(accept_cb, this);
server->begin();

// Server is now listening on port 8080
// When begin() is called, it automatically registers with PosixAsyncTCPManager
```

### Accepting Connections and Handling Data

```cpp
void MyHandler::handleNewClient(AsyncClient* client) {
    printf("Client connected from %s:%d\n",
           client->remoteIP().toString().c_str(),
           client->remotePort());
    
    // Set up receive callback
    auto recv_cb = [](void* arg, AsyncClient* c, uint8_t* data, size_t len) {
        auto* handler = (MyHandler*)arg;
        handler->onData(c, data, len);
    };
    
    client->onData(recv_cb, this);
    
    // Set up disconnect callback
    auto disconnect_cb = [](void* arg, AsyncClient* c) {
        auto* handler = (MyHandler*)arg;
        handler->onDisconnect(c);
    };
    
    client->onDisconnect(disconnect_cb, this);
}

void MyHandler::onData(AsyncClient* client, uint8_t* data, size_t len) {
    // Process incoming data
    // This is called in polling thread context—keep it brief
    printf("Received %d bytes\n", len);
    
    // Send response
    const char* response = "HTTP/1.1 200 OK\r\n\r\n";
    client->write(response, strlen(response));
}

void MyHandler::onDisconnect(AsyncClient* client) {
    printf("Client disconnected\n");
}
```

### Creating Client Connections

```cpp
// Outgoing connection (if needed by FluidNC)
auto client = std::make_shared<AsyncClient>();

auto connect_cb = [](void* arg, AsyncClient* c) {
    auto* handler = (MyHandler*)arg;
    printf("Connected to server\n");
    
    handler->sendRequest(c);
};

auto error_cb = [](void* arg, AsyncClient* c, int err) {
    printf("Connection error: %d\n", err);
};

client->onConnect(connect_cb, this);
client->onError(error_cb, this);

bool ok = client->connect(IPAddress(192, 168, 1, 100), 80);
if (!ok) {
    printf("Failed to initiate connection\n");
}
```

### Initialization in Protocol

In `Protocol.cpp`, ensure the async manager is started at startup:

```cpp
void Protocol::setup() {
    // Existing protocol setup...
    
    #if defined(__linux__) || defined(__APPLE__)
        PosixAsyncTCPManager::getInstance().begin();
    #endif
}
```

And stopped at shutdown:

```cpp
void Protocol::end() {
    #if defined(__linux__) || defined(__APPLE__)
        PosixAsyncTCPManager::getInstance().end();
    #endif
    
    // Existing protocol cleanup...
}
```

## API Reference

### AsyncClient

#### Connection Methods

```cpp
bool connect(IPAddress ip, uint16_t port);
bool connect(const char* host, uint16_t port);
void close(bool now = false);
void stop();
```

#### State Queries

```cpp
bool connected() const;
bool connecting() const;
bool disconnecting() const;
bool disconnected() const;
bool free() const;
uint8_t state() const;
bool canSend() const;
size_t space() const;
```

#### Data Transfer

```cpp
size_t write(const char* data);
size_t write(const char* data, size_t size, uint8_t apiflags = 0);
size_t add(const char* data, size_t size, uint8_t apiflags = 0);
bool send();
size_t ack(size_t len);
void ackLater();
```

#### Configuration

```cpp
void setRxTimeout(uint32_t timeout);  // Seconds
void setAckTimeout(uint32_t timeout);
void setNoDelay(bool nodelay);
```

#### Network Information

```cpp
IPAddress remoteIP() const;
uint16_t remotePort() const;
IPAddress localIP() const;
uint16_t localPort() const;
```

#### Callbacks

```cpp
void onConnect(AcConnectHandler cb, void* arg);
void onDisconnect(AcConnectHandler cb, void* arg);
void onAck(AcAckHandler cb, void* arg);
void onError(AcErrorHandler cb, void* arg);
void onData(AcDataHandler cb, void* arg);
void onTimeout(AcTimeoutHandler cb, void* arg);
void onPoll(AcPollHandler cb, void* arg);
```

### AsyncServer

```cpp
AsyncServer(uint16_t port);
AsyncServer(IPAddress addr, uint16_t port);

void begin();
void end();
uint8_t status() const;
void onClient(AcConnectHandler cb, void* arg);
```

### PosixAsyncTCPManager

```cpp
static PosixAsyncTCPManager& getInstance();
void begin();
void end();
```

## Differences from ESP32 AsyncTCP

### Similarities
- **Same callback signatures** — Makes porting code trivial
- **Non-blocking API** — All operations are async
- **State tracking** — Full connection state machine
- **Multi-client** — Supports multiple simultaneous connections

### Key Differences

| Feature | ESP32 AsyncTCP | PosixAsyncTCP |
|---------|---|---|
| Underlying I/O | LWIP stack | POSIX sockets |
| Threading | FreeRTOS tasks | std::thread |
| Polling interval | 2ms | 125ms (tunable) |
| Memory model | ESP32 specific | Standard malloc |
| Flow control | TCP window-based | EAGAIN/EWOULDBLOCK |
| Real-time | No (embedded) | Depends on OS scheduler |

### Compatibility Notes

1. **Timeouts are in seconds** (not milliseconds) for RX timeout to match the API
2. **No special IP handling** — Use standard POSIX sockets
3. **Callback context** — Called from polling thread, not FreeRTOS task
4. **Thread safety** — Library uses mutexes; callbacks must not block

## Testing

### Unit Tests

Run automated tests with:

```bash
cd FluidNC/posix/PosixAsyncTCP
cmake -B build
cd build
cmake --build . --target test
```

Tests cover:
- Server creation and binding
- Client connection and disconnection
- Data transmission bidirectionally
- Multiple simultaneous clients
- Timeout behavior
- Error handling
- State machine transitions

### Integration Testing

To test with real FluidNC network features:

1. Build with POSIX platform enabled
2. Run FluidNC with WebUI on localhost
3. Connect via standard network tests:

```bash
# In another terminal
curl http://localhost:8080/
curl -X POST http://localhost:8080/command -d "G0 X10 Y10"

# Or use WebSocket client
wscat -c ws://localhost:8080/ws
```

## Performance Considerations

1. **Polling Interval**: Default 125ms (same as LWIP). Change in `PosixAsyncTCP.cpp`:
   ```cpp
   struct timeval tv;
   tv.tv_sec = 0;
   tv.tv_usec = 125000;  // Adjust here
   ```

2. **Buffer Sizes**: Send/receive buffers are dynamically allocated. No hardcoded limits.

3. **Thread Priority**: Polling thread runs at default priority. On Linux, consider:
   ```cpp
   pthread_t thread_id = _poll_thread.native_handle();
   sched_param param;
   param.sched_priority = sched_get_priority_max(SCHED_FIFO);
   pthread_setschedparam(thread_id, SCHED_FIFO, &param);  // Requires CAP_SYS_NICE
   ```

## Troubleshooting

### Connection Refused
- Ensure server is started (`begin()` called)
- Check port is not already in use: `lsof -i :<port>`
- Verify firewall isn't blocking connections

### Data Not Received
- Check that `onData()` callback is registered
- Verify data is being sent (`write()` called)
- Look for errors via `onError()` callback

### Timeout Never Fires
- RX timeout only triggers on lack of incoming data
- If you're actively receiving, timer resets
- Set via `setRxTimeout()` in seconds

### Thread Safety Issues
- All callbacks run in polling thread
- Don't call blocking operations (sleep, network calls) from callbacks
- Use async patterns or queue messages for thread handoff

## Future Enhancements

1. **SSL/TLS Support** — Using OpenSSL library
2. **Configurable Poll Interval** — Allow tuning for different use cases
3. **Flow Control Callbacks** — Drain buffer notification
4. **IPv6 Support** — Extend for IPv6 addresses
5. **UDP Support** — Add AsyncUDP for telemetry
6. **Performance Metrics** — Built-in throughput/latency tracking

## See Also

- [FluidNC WebUI Documentation](../../FluidNC/WebUI/README.md)
- [POSIX Socket Programming](https://man7.org/linux/man-pages/man7/socket.7.html)
- [ESP32 AsyncTCP Reference](https://github.com/me-no-dev/AsyncTCP)

---

**For questions or issues, refer to FluidNC issue tracker or documentation.**
