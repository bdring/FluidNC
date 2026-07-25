# websocketpp Integration Plan for FluidNC

## Objective
Replace the custom WebSocket implementation with websocketpp library to:
- Stop connection drop (error 1006) after successful handshake
- Use production-grade, well-tested WebSocket implementation
- Reduce custom socket handling code

## Current Status
- **Custom implementation**: ~600 lines, handshake works, post-handshake connection drops
- **Browser behavior**: error 1006 (abnormal closure) immediately after onopen()
- **Root cause**: Unknown socket state issue in custom implementation
-Decision: Use websocketpp instead of further debugging

## websocketpp Requirements

### Core Libraries
1. **websocketpp** - WebSocket server library (header-only)
   - Source: https://github.com/zaphoyd/websocketpp
   - Version: Latest (v0.8.2 is current stable)
   - License: BSD 3-Clause
   
2. **Asio** - Async I/O library (websocketpp dependency)
   - Standalone Asio (not Boost.Asio needed for this project)
   - Latest version recommended
   - License: Boost Software License

### POSIX Platform Considerations
- Platform: `native` (PlatformIO)
- C++ Standard: C++20 (already configured)
- Threading: pthreads (already available)
- DNS: system resolver (built-in)

## Integration Approach

### Option 1: Manual Download (Recommended for now)
1. Download websocketpp and asio header files
2. Place in `FluidNC/include/websocketpp/` and `FluidNC/include/asio/`
3. Add `-IFluidNC/include` to build_flags in platformio.ini

### Option 2: PlatformIO Library Manager
- Check if either library available on PlatformIO registry
- Add to lib_deps if available
- Fallback to Option 1 if not available

## Implementation Phases

### Phase 1: Dependency Setup
- [ ] Download websocketpp headers (or add via lib manager)
- [ ] Download asio headers (or add via lib manager)
- [ ] Verify includes in build

### Phase 2: Create WebSocket Server Class
- [ ] Create `FluidNC/posix/WebSocketppServer.h/cpp`
- [ ] Implement using websocketpp::server API
- [ ] Handle client connections
- [ ] Implement message send/receive

### Phase 3: Integration into Console
- [ ] Update Console.cpp to use new server
- [ ] Remove old SimulatorWebSocketServer references
- [ ] Test compilation

### Phase 4: Testing
- [ ] Start server and verify listening
- [ ] Test browser WebSocket client (localhost:9000)
- [ ] Verify connection stays open after handshake
- [ ] Test message exchange

## Estimated websocketpp Code Structure

```cpp
// Minimal websocketpp server example
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;

class WebSocketppServer {
    server m_endpoint;
    
    void on_open(websocketpp::connection_hdl hdl) {
        // Client connected
    }
    
    void on_close(websocketpp::connection_hdl hdl) {
        // Client disconnected
    }
    
    void on_message(websocketpp::connection_hdl hdl, 
                    server::message_ptr msg) {
        // Handle message
    }
    
public:
    void init(uint16_t port) {
        m_endpoint.set_open_handler(&WebSocketppServer::on_open);
        m_endpoint.set_close_handler(&WebSocketppServer::on_close);
        m_endpoint.set_message_handler(&WebSocketppServer::on_message);
        
        m_endpoint.init_asio();
        m_endpoint.listen(websocketpp::lib::asio::ip::tcp
                         ::endpoint(websocketpp::lib::asio
                                   ::ip::tcp::v4(), port));
        m_endpoint.start_accept();
        
        // Run in thread...
        std::thread t([this]() { 
            m_endpoint.run(); 
        });
        t.detach();
    }
};
```

## Expected Benefits
- ✅ Professional WebSocket implementation
- ✅ Proper RFC 6455 compliance (battle-tested)
- ✅ Robust error handling
- ✅ No custom socket management
- ✅ Async I/O via Asio (tested under load)
- ✅ Message framing/unframing handled automatically

## Risks & Mitigation
| Risk | Cause | Mitigation |
|------|-------|-----------|
| Dependency not in PlatformIO | Library registry limitations | Manual header installation |
| Asio complexity | Async framework overhead | Use simple server config |
| Build failures | Include path issues | Add comprehensive build_flags |
| Performance | Thread overhead | Single-threaded event loop (Asio native) |

## Success Criteria
1. Server starts without errors
2. Browser connects and onopen() fires
3. Connection remains open (no error 1006)
4. Browser can send/receive WebSocket messages
5. Server handles multiple simultaneous connections

## Timeline
- Phase 1 (Setup): 15 minutes
- Phase 2 (Implementation): 30-45 minutes
- Phase 3 (Integration): 15 minutes
- Phase 4 (Testing): 15 minutes
- **Total: ~1-2 hours**

## next Steps
1. Check if websocketpp available in PlatformIO registry
2. Download required headers
3. Implement new WebSocketppServer class
4. Integrate and test
