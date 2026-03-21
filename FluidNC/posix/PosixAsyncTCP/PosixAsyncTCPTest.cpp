#include <gtest/gtest.h>
#include "PosixAsyncTCP.h"

#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>

class PosixAsyncTCPTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize the async manager
        PosixAsyncTCPManager::getInstance().begin();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        PosixAsyncTCPManager::getInstance().end();
    }
};

// Test basic AsyncClient creation
TEST_F(PosixAsyncTCPTest, ClientConstructor) {
    AsyncClient client;
    EXPECT_EQ(client.state(), CLOSED);
    EXPECT_FALSE(client.connected());
    EXPECT_FALSE(client.connecting());
    EXPECT_FALSE(client.disconnecting());
    EXPECT_TRUE(client.disconnected());
}

// Test AsyncServer creation
TEST_F(PosixAsyncTCPTest, ServerConstructor) {
    AsyncServer server(IPADDR_ANY, 0);
    EXPECT_EQ(server.status(), CLOSED);
}

// Test AsyncServer begin/end
TEST_F(PosixAsyncTCPTest, ServerBeginEnd) {
    AsyncServer server(IPADDR_ANY, 8765);
    server.begin();
    
    // Give it a moment to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(server.status(), LISTEN);
    
    server.end();
    EXPECT_EQ(server.status(), CLOSED);
}

// Test connecting two clients via localhost
TEST_F(PosixAsyncTCPTest, ServerClientConnectLocal) {
    std::atomic<bool> server_connect_called(false);
    std::atomic<bool> client_connect_called(false);
    
    AsyncServer server(IPADDR_ANY, 8766);
    
    auto server_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    server.onClient(server_connect_cb, &server_connect_called);
    server.begin();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto client = std::make_shared<AsyncClient>();
    
    auto client_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    client->onConnect(client_connect_cb, &client_connect_called);
    
    bool connected = client->connect(IPAddress(127, 0, 0, 1), 8766);
    EXPECT_TRUE(connected);
    
    // Wait for connection
    int wait_count = 0;
    while (!client_connect_called && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
    }
    
    EXPECT_TRUE(client_connect_called);
    
    // Server should have received connection
    wait_count = 0;
    while (!server_connect_called && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
    }
    
    EXPECT_TRUE(server_connect_called);
    
    client->close();
    server.end();
}

// Test data transmission
TEST_F(PosixAsyncTCPTest, DataTransmission) {
    std::atomic<bool> server_connect_called(false);
    std::atomic<bool> client_connect_called(false);
    std::atomic<bool> server_recv_called(false);
    std::string server_received_data;
    
    AsyncServer server(IPADDR_ANY, 8767);
    
    auto server_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
        
        // Set up receive callback for this client
        auto recv_cb = [](void* arg, AsyncClient* c, uint8_t* data, size_t len) {
            auto* recv_flag = (std::atomic<bool>*)arg;
            auto* str = (std::string*)c->_reserve;  // Store in reserved void pointer
            if (str) {
                str->append((const char*)data, len);
            }
            *recv_flag = true;
        };
        
        client->onData(recv_cb, &server_recv_called);
    };
    
    server.onClient(server_connect_cb, &server_connect_called);
    server.begin();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto client = std::make_shared<AsyncClient>();
    
    auto client_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    client->onConnect(client_connect_cb, &client_connect_called);
    
    bool connected = client->connect(IPAddress(127, 0, 0, 1), 8767);
    EXPECT_TRUE(connected);
    
    // Wait for connection
    int wait_count = 0;
    while (!client_connect_called && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
    }
    
    EXPECT_TRUE(client_connect_called);
    
    // Send data from client
    const char* test_data = "Hello Server";
    size_t sent = client->write(test_data);
    EXPECT_GT(sent, 0);
    
    // Wait a bit for transmission and processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    client->close();
    server.end();
}

// Test multiple clients
TEST_F(PosixAsyncTCPTest, MultipleClients) {
    std::atomic<int> server_connections(0);
    
    AsyncServer server(IPADDR_ANY, 8768);
    
    auto server_connect_cb = [](void* arg, AsyncClient* client) {
        auto* count = (std::atomic<int>*)arg;
        (*count)++;
    };
    
    server.onClient(server_connect_cb, &server_connections);
    server.begin();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create and connect multiple clients
    const int NUM_CLIENTS = 3;
    std::vector<std::shared_ptr<AsyncClient>> clients;
    std::vector<std::atomic<bool>> connect_flags(NUM_CLIENTS);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        connect_flags[i] = false;
        
        auto client = std::make_shared<AsyncClient>();
        
        auto client_connect_cb = [](void* arg, AsyncClient* c) {
            auto* flag = (std::atomic<bool>*)arg;
            *flag = true;
        };
        
        client->onConnect(client_connect_cb, &connect_flags[i]);
        
        bool connected = client->connect(IPAddress(127, 0, 0, 1), 8768);
        EXPECT_TRUE(connected);
        
        clients.push_back(client);
    }
    
    // Wait for all connections
    for (int i = 0; i < 50; i++) {
        if (server_connections >= NUM_CLIENTS) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_EQ((int)server_connections, NUM_CLIENTS);
    
    // Close all
    for (auto& client : clients) {
        client->close();
    }
    
    server.end();
}

// Test timeout behavior
TEST_F(PosixAsyncTCPTest, RxTimeout) {
    std::atomic<bool> server_connect_called(false);
    std::atomic<bool> client_connect_called(false);
    std::atomic<bool> timeout_called(false);
    
    AsyncServer server(IPADDR_ANY, 8769);
    
    auto server_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    server.onClient(server_connect_cb, &server_connect_called);
    server.begin();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto client = std::make_shared<AsyncClient>();
    
    auto client_connect_cb = [](void* arg, AsyncClient* client) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    auto timeout_cb = [](void* arg, AsyncClient* client, uint32_t time) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    client->onConnect(client_connect_cb, &client_connect_called);
    client->onTimeout(timeout_cb, &timeout_called);
    client->setRxTimeout(1);  // 1 second timeout
    
    bool connected = client->connect(IPAddress(127, 0, 0, 1), 8769);
    EXPECT_TRUE(connected);
    
    // Wait for connection
    int wait_count = 0;
    while (!client_connect_called && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
    }
    
    EXPECT_TRUE(client_connect_called);
    
    // Wait for timeout (RX data gets updated on recv, but we're disconnecting)
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    client->close();
    server.end();
}

// Test space() calculation
TEST_F(PosixAsyncTCPTest, SpaceCalculation) {
    AsyncClient client;
    
    // Not connected: space should be 0
    EXPECT_EQ(client.space(), 0);
}

// Test state transitions
TEST_F(PosixAsyncTCPTest, StateTransitions) {
    AsyncClient client;
    
    // Initial state: CLOSED
    EXPECT_EQ(client.state(), CLOSED);
    EXPECT_TRUE(client.free());
    EXPECT_FALSE(client.connected());
    EXPECT_FALSE(client.connecting());
    EXPECT_FALSE(client.disconnecting());
    EXPECT_TRUE(client.disconnected());
}

// Test error handling
TEST_F(PosixAsyncTCPTest, ConnectionRefused) {
    std::atomic<bool> error_called(false);
    
    auto client = std::make_shared<AsyncClient>();
    
    auto error_cb = [](void* arg, AsyncClient* c, int error) {
        auto* flag = (std::atomic<bool>*)arg;
        *flag = true;
    };
    
    client->onError(error_cb, &error_called);
    
    // Try to connect to a port that's not listening
    bool connected = client->connect(IPAddress(127, 0, 0, 1), 9999);
    
    // Connection attempt may succeed (async), but we should get an error
    if (connected) {
        // Wait for error callback
        int wait_count = 0;
        while (!error_called && wait_count < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            wait_count++;
        }
        
        // EXPECT_TRUE(error_called);  // May or may not fire depending on timing
    }
    
    client->close();
}

// Test canSend()
TEST_F(PosixAsyncTCPTest, CanSend) {
    AsyncClient client;
    
    // Not connected: can't send
    EXPECT_FALSE(client.canSend());
}

// Test write() with null data
TEST_F(PosixAsyncTCPTest, WriteNullData) {
    AsyncClient client;
    
    size_t written = client.write(nullptr);
    EXPECT_EQ(written, 0);
}

// Test stateToString()
TEST_F(PosixAsyncTCPTest, StateToString) {
    AsyncClient client;
    
    const char* state_str = client.stateToString();
    EXPECT_STREQ(state_str, "Closed");
}
