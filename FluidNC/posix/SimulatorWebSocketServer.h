#pragma once

// macOS compatibility for websocket library
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
using std::min;

// macOS byte-order functions (they're called differently than Linux)
#ifdef __APPLE__
#include <machine/endian.h>
#define htobe16 htons
#define htobe32 htonl
#define htobe64(x) (((uint64_t)htonl((x) & 0xffffffff) << 32) | htonl((x) >> 32))
#define be16toh ntohs
#define be32toh ntohl
#define be64toh(x) (((uint64_t)ntohl((x) & 0xffffffff) << 32) | ntohl((x) >> 32))
#endif

#ifndef MSG_MORE
#define MSG_MORE 0  // macOS doesn't have MSG_MORE, so use 0
#endif

#include <websocket.h>
#include <string>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace SimulatorWS {

/**
 * Simulator WebSocket Server using MengRao/websocket library
 * Single header-only RFC 6455 compliant WebSocket implementation
 * Supports one connected client at a time
 */
class SimulatorWebSocketServer {
public:
    // Connection user data - attach simulator-specific info to each connection
    struct ConnData {
        uint64_t connected_time;
    };

    // Forward declare the websocket server type
    using WSServer = websocket::WSServer<SimulatorWebSocketServer, ConnData>;
    using WSConnection = WSServer::Connection;

    static SimulatorWebSocketServer& instance();

    // Server lifecycle
    void init(uint16_t port = 9000);
    void deinit();
    
    // Process position updates from simulator engine queue
    void processAndBroadcastPositionUpdates();

    // Configuration
    uint16_t getPort() const { return _port; }
    bool isRunning() const { return _running; }
    bool hasActiveConnection() const { 
        std::lock_guard<std::mutex> lock(_connection_mutex);
        return _current_connection != nullptr; 
    }

    // WebSocket server callbacks (must be public for library to call them)
    bool onWSConnect(WSConnection& conn, const char* request_uri, const char* host,
                     const char* origin, const char* protocol, const char* extensions,
                     char* resp_protocol, uint32_t resp_protocol_size,
                     char* resp_extensions, uint32_t resp_extensions_size);

    void onWSClose(WSConnection& conn, uint16_t status_code, const char* reason);

    void onWSMsg(WSConnection& conn, uint8_t opcode, const uint8_t* payload, uint32_t pl_len);

    // Implement the segment handler too (required by library even if not using segments)
    void onWSSegment(WSConnection& conn, uint8_t opcode, const uint8_t* payload,
                     uint32_t pl_len, uint32_t pl_start_idx, bool fin) {
        // Not used in this configuration
    }

private:
    SimulatorWebSocketServer() = default;
    ~SimulatorWebSocketServer() = default;

    // Prevent copying
    SimulatorWebSocketServer(const SimulatorWebSocketServer&) = delete;
    SimulatorWebSocketServer& operator=(const SimulatorWebSocketServer&) = delete;

    uint16_t _port = 9000;
    bool _running = false;
    WSServer* _server = nullptr;
    WSConnection* _current_connection = nullptr;  // Single active connection (set in callbacks)
    mutable std::mutex _connection_mutex;  // Protect _current_connection access
};

} // namespace SimulatorWS

// C wrapper for simulator_engine.c to check if a client is connected
extern "C" {
    bool simulator_ws_has_client(void);
}
