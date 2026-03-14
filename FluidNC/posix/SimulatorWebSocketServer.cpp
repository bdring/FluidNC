#include "SimulatorWebSocketServer.h"

#include <stdio.h>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include "simulator_engine.h"

namespace SimulatorWS {

// Singleton instance for callback access
static SimulatorWebSocketServer* g_instance = nullptr;

SimulatorWebSocketServer& SimulatorWebSocketServer::instance() {
    static SimulatorWebSocketServer s_instance;
    g_instance = &s_instance;
    return s_instance;
}

void SimulatorWebSocketServer::init(uint16_t port) {
    if (_running) {
        return;  // Already running
    }

    _port = port;

    // Create the WebSocket server
    _server = new WSServer();
    if (!_server->init("0.0.0.0", port)) {
        fprintf(stderr, "[SimulatorWS] Server init failed: %s\n", _server->getLastError());
        delete _server;
        _server = nullptr;
        return;
    }

    fprintf(stderr, "[SimulatorWS] Server initialized on port %d\n", port);

    _running = true;

    // Start WebSocket I/O thread - handles polling, connections, incoming messages
    std::thread([this]() {
        fprintf(stderr, "[SimulatorWS] WebSocket I/O thread started\n");
        while (_running) {
            _server->poll(this);
            // Small sleep to let other threads run
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        fprintf(stderr, "[SimulatorWS] WebSocket I/O thread ended\n");
    }).detach();

    // Start position broadcaster thread - drains queue and sends updates
    std::thread([this]() {
        fprintf(stderr, "[SimulatorWS] Position broadcaster thread started\n");
        while (_running) {
            processAndBroadcastPositionUpdates();
            // Small sleep to prevent busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        fprintf(stderr, "[SimulatorWS] Position broadcaster thread ended\n");
    }).detach();
}

void SimulatorWebSocketServer::deinit() {
    _running = false;
    if (_server) {
        delete _server;
        _server = nullptr;
    }
    fprintf(stderr, "[SimulatorWS] Server stopped\n");
}

void SimulatorWebSocketServer::processAndBroadcastPositionUpdates() {
    static bool last_connection_state = false;

    // Only process if we have an active connection
    bool has_connection = false;
    {
        std::lock_guard<std::mutex> lock(_connection_mutex);
        has_connection = (_current_connection != nullptr && _current_connection->isConnected());
    }

    // Log only on state change
    if (has_connection != last_connection_state) {
        fprintf(stderr, "[SimulatorWS-Broadcaster] Connection state changed: %s\n", 
                has_connection ? "CONNECTED" : "DISCONNECTED");
        last_connection_state = has_connection;
    }

    if (!has_connection) {
        return;
    }

    // Drain all queued position updates
    queue_message_t msg;
    unsigned int dequeue_count = 0;
    while (simulator_queue_dequeue(&msg)) {
        dequeue_count++;
        
        // Format as JSON: {"type":"position","x":...,"y":...,"z":...,"a":...,"b":...,"c":...,"final":true/false}
        char buffer[256];
        int len = snprintf(buffer, sizeof(buffer),
            "{\"type\":\"position\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"a\":%.3f,\"b\":%.3f,\"c\":%.3f,\"final\":%s}",
            msg.position.x, msg.position.y, msg.position.z,
            msg.position.a, msg.position.b, msg.position.c,
            msg.is_final ? "true" : "false");
        
        if (len > 0 && len < (int)sizeof(buffer)) {
            // Send position update to the connected client
            std::lock_guard<std::mutex> lock(_connection_mutex);
            if (_current_connection && _current_connection->isConnected()) {
                _current_connection->send(websocket::OPCODE_TEXT, (const uint8_t*)buffer, len);
            } else {
                fprintf(stderr, "[SimulatorWS-Broadcaster] Connection lost while sending\n");
                break;
            }
        }
    }

    // Log batch summary only if we sent updates
    if (dequeue_count > 0) {
        fprintf(stderr, "[SimulatorWS-Broadcaster] Sent %u position updates\n", dequeue_count);
    }
}

bool SimulatorWebSocketServer::onWSConnect(WSConnection& conn, const char* request_uri,
                                          const char* host, const char* origin,
                                          const char* protocol, const char* extensions,
                                          char* resp_protocol, uint32_t resp_protocol_size,
                                          char* resp_extensions, uint32_t resp_extensions_size) {
    struct sockaddr_in addr;
    conn.getPeername(addr);

    fprintf(stderr, "[SimulatorWS] New WebSocket connection from %s:%d\n",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    fprintf(stderr, "[SimulatorWS] Request URI: %s\n", request_uri);
    if (host) fprintf(stderr, "[SimulatorWS] Host: %s\n", host);
    if (origin) fprintf(stderr, "[SimulatorWS] Origin: %s\n", origin);

    // Store connection time
    conn.user_data.connected_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Store this as the current active connection (single connection assumed)
    {
        std::lock_guard<std::mutex> lock(_connection_mutex);
        _current_connection = &conn;
    }
    fprintf(stderr, "[SimulatorWS] Connection stored for position updates\n");

    // Accept the connection
    return true;
}

void SimulatorWebSocketServer::onWSClose(WSConnection& conn, uint16_t status_code,
                                        const char* reason) {
    fprintf(stderr, "[SimulatorWS] Connection closed. Status: %d, Reason: %s\n",
            status_code, reason ? reason : "");

    // Clear the connection pointer if this is the current one
    {
        std::lock_guard<std::mutex> lock(_connection_mutex);
        if (_current_connection == &conn) {
            _current_connection = nullptr;
            fprintf(stderr, "[SimulatorWS] Cleared active connection\n");
        }
    }
}

void SimulatorWebSocketServer::onWSMsg(WSConnection& conn, uint8_t opcode,
                                      const uint8_t* payload, uint32_t pl_len) {
    if (opcode == websocket::OPCODE_PING) {
        // Respond to PING with PONG
        fprintf(stderr, "[SimulatorWS] PING received, sending PONG\n");
        conn.send(websocket::OPCODE_PONG, payload, pl_len);
        return;
    }

    if (opcode == websocket::OPCODE_TEXT) {
        fprintf(stderr, "[SimulatorWS] Text message received: %d bytes\n", pl_len);
        // Echo acknowledgment
        conn.send(websocket::OPCODE_TEXT, (const uint8_t*)"{\"type\":\"ack\"}", 14);
        return;
    }

    if (opcode == websocket::OPCODE_BINARY) {
        fprintf(stderr, "[SimulatorWS] Binary message received: %d bytes\n", pl_len);
        return;
    }

    fprintf(stderr, "[SimulatorWS] Unhandled opcode: %d\n", opcode);
}

} // namespace SimulatorWS

// C wrapper for simulator_engine.c to check if WebSocket client is connected
extern "C" {
    bool simulator_ws_has_client(void) {
        static bool last_state = false;
        bool has_client = SimulatorWS::SimulatorWebSocketServer::instance().hasActiveConnection();
        
        // Log only on state change
        if (has_client != last_state) {
            fprintf(stderr, "[simulator_ws_has_client] Client state changed: %s\n", 
                    has_client ? "CONNECTED" : "DISCONNECTED");
            last_state = has_client;
        }
        
        return has_client;
    }
}
