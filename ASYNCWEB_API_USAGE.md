# AsyncWebSocket/AsyncWebServer API Usage in FluidNC

This document catalogues all the AsyncWeb API usage patterns found in the FluidNC codebase across the specified WebUI files.

---

## AsyncWebServer Methods

### Server Creation & Lifecycle
- **Constructor**: `AsyncWebServer(_port)` - Create server with port number
- **begin()** - Start the web server listening
- **delete/destruction** - Clean up server instance

### Route Registration
- **on(path, HTTP_METHOD, callback)** - Register route handler with single callback
  - Usage: `_webserver->on("/", HTTP_ANY, handle_root)`
  - HTTP_METHOD examples: `HTTP_ANY`, `HTTP_GET`, `HTTP_POST`, `HTTP_PUT`, `HTTP_DELETE`, `HTTP_HEAD`, `HTTP_OPTIONS`, `HTTP_PATCH`

- **on(path, HTTP_METHOD, callback, upload_callback)** - Register route with upload handler
  - Usage: `_webserver->on("/files", HTTP_ANY, handleFileList, LocalFSFileupload)`
  - Upload callback signature: `void(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final)`

- **onNotFound(callback)** - Register 404 handler
  - Usage: `_webserver->onNotFound(handle_not_found)`

### Handler Management
- **addHandler(handler)** - Add custom handler/middleware
  - Usage: `_webserver->addHandler(flash_dav)`, `_webserver->addHandler(_socket_server)`
  - Parameter types: WebDAV handlers, AsyncWebSocket instances

- **addMiddlewares(initializer_list)** - Add middleware layer
  - Usage: `_webserver->addMiddlewares({ _headerFilter })`

### Header/Cookie Management
- **hasArg(name)** - Check if GET/POST argument exists (legacy mode)
- **arg(name)** - Get GET/POST argument value (legacy mode)
- **hasHeader(name)** - Check if header exists
- **header(name)** - Get header value by name  
- **sendHeader(name, value)** - Send response header (legacy mode)
  - Usage: `_webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0")`
  - Usage: `_webserver->sendHeader(T_Cache_Control, T_no_cache)`

### Client Management
- **client()** - Get current client (legacy mode context)

---

## AsyncWebServerRequest Methods

### Request Information
- **method()** - Get HTTP method
  - Returns: `HTTP_GET`, `HTTP_POST`, etc.
  - Usage: `if (request->method() == HTTP_GET)`

- **url()** - Get request URL path
  - Returns: `String` (or const char*)
  - Usage: `request->url().c_str()`

- **client()** - Get underlying AsyncClient
  - Returns: `AsyncClient*`
  - Usage: `request->client()->remoteIP()`, `request->client()->close()`

### Query Parameter Handling
- **hasParam(name)** - Check if parameter exists (GET/POST/URL)
  - Usage: `if (request->hasParam("cmd"))`

- **getParam(name)** - Get parameter by name
  - Returns: `AsyncWebParameter*`
  - Chaining: `request->getParam("cmd")->value()`
  - Value conversion: `.value().toInt()`, `.value().c_str()`

### Header Handling
- **hasHeader(name)** - Check if request header exists
  - Usage: `if (request->hasHeader("Accept-Encoding"))`

- **getHeader(name)** - Get header by name
  - Returns: `AsyncWebHeader*`
  - Chaining: `request->getHeader("Cookie")->value().c_str()`

### Response Building
- **beginResponse(code, contentType, content)** - Create simple response
  - Signature: `AsyncWebServerResponse* beginResponse(int code, const char* contentType, const char* content)`
  - Usage: `request->beginResponse(304)`, `request->beginResponse(200, "text/html", content)`
  - Advanced: `request->beginResponse(code, contentType, file->size(), callback)` - With stream callback

- **beginChunkedResponse(contentType, callback)** - Create chunked streaming response
  - Signature: `AsyncWebServerResponse* beginChunkedResponse(const char* contentType, AwsGetCurrentBuffer* callback)`
  - Callback receives: `(uint8_t* buffer, size_t maxLen, size_t total)`
  - Returns bytes written

- **beginResponseStream(contentType)** - Create streaming response
  - Signature: `AsyncResponseStream* beginResponseStream(const char* contentType)`
  - Returns: `AsyncResponseStream*` for progressive output

### Response Operations (on Response Object)
- **send(response)** - Send response to client
  - Overloads:
    - `send(AsyncWebServerResponse*)`
    - `send(int code)` 
    - `send(int code, const char* contentType, const char* content)`
    - `send(int code, const char* contentType, size_t size, AwsGetCurrentBuffer* callback)`

- **addHeader(name, value)** - Add response header
  - Usage: `response->addHeader("Set-Cookie", cookieValue)`
  - Usage: `response->addHeader("Cache-Control", "no-cache")`
  - Usage: `response->addHeader("Content-Encoding", "gzip")`
  - Usage: `response->addHeader("ETag", hash)`

- **setCode(code)** - Set HTTP status code
  - Usage: `response->setCode(200)`, `response->setCode(503)`

### Client Control & Lifecycle
- **redirect(url)** - Redirect to another URL
  - Usage: `request->redirect("/")`

- **onDisconnect(callback)** - Register callback for client disconnect
  - Signature: `onDisconnect(std::function<void()> callback)`
  - Usage: `request->onDisconnect([request, file]() { delete file; })`

---

## AsyncWebSocket Methods

### Configuration
- **addMiddleware(middleware)** - Add WebSocket middleware
  - Signature: `addMiddleware(std::function<void(AsyncWebServerRequest*, ArMiddlewareNext)> callback)`
  - Middleware can inspect request before connection
  - Must call `next()` to continue chain
  - Usage:
    ```cpp
    _socket_server->addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        current_session = getSessionCookie(request);
        next();  // continue middleware chain
    });
    ```

- **onEvent(callback)** - Register WebSocket event handler
  - Signature: `onEvent(std::function<void(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t)> callback)`
  - Parameters to callback:
    - `AsyncWebSocket* server` - The server instance
    - `AsyncWebSocketClient* client` - The client connection
    - `AwsEventType type` - Event type (see Event Types below)
    - `void* arg` - Event-specific argument (e.g., `AwsFrameInfo*` for data events)
    - `uint8_t* data` - Payload data
    - `size_t len` - Payload length

### Client Management
- **client(clientNum)** - Get client by ID
  - Returns: `AsyncWebSocketClient*`
  - Usage: `_socket_server->client(_clientNum)`
  - Can check null: `if (_server->client(_clientNum))`

- **cleanupClients()** - Remove disconnected/stale clients
  - Usage: `_socket_server->cleanupClients()`

### Broadcasting
- **textAll(message)** - Send text message to all connected clients
  - Usage: `_server->textAll("PING\n")`

- **binaryAll(data, length)** - Send binary message to all clients (not actively used)
  - Commented out in code: `// _server->binaryAll(out, outlen)`

### URL Management
- **url()** - Get WebSocket URL path
  - Returns: URI path as pointer
  - Usage: `std::string uri((char*)server->url())`

---

## AsyncWebSocketClient Methods

### Identification
- **id()** - Get client unique ID
  - Returns: `uint32_t`
  - Usage: `uint32_t num = client->id()`

- **remoteIP()** - Get client IP address
  - Returns: `IPAddress`
  - Usage: `IPAddress ip = client->remoteIP()`

### Connection Management
- **setCloseClientOnQueueFull(bool)** - Configure queue overflow behavior
  - Usage: `_server->client(_clientNum)->setCloseClientOnQueueFull(false)` (keep connection open)

### Message Transmission
- **text(clientNum, message)** - Send text message to specific client
  - Signature: `text(uint32_t num, const char* message)` (static on server)
  - Returns: `bool` (success/failure)
  - Usage: `_server->text(_clientNum, s.c_str())`

- **binary(clientNum, data, length)** - Send binary message to specific client
  - Signature: `binary(uint32_t num, const uint8_t* message, size_t len)` (static on server)
  - Returns: `bool` (success/failure)
  - Usage: `if (!_server->binary(_clientNum, out, outlen))`

### Queue Management
- **queueLen()** - Get current queue length (pending messages)
  - Returns: `size_t`
  - Usage: `_server->client(_clientNum)->queueLen() >= max(WS_MAX_QUEUED_MESSAGES - 2, 1)`

- **queueIsFull()** - Check if queue is full
  - Returns: `bool`
  - Usage: `if (_server->client(_clientNum)->queueIsFull())`

---

## AsyncClient Methods (from AsyncTCP)

### Remote Connection Info
- **getRemoteAddress()** - Get client IP as numerical address
  - Returns: `uint32_t` (raw IP)
  - Usage: `IPAddress(client->getRemoteAddress())`

- **getRemotePort()** - Get client port number
  - Returns: `uint16_t`
  - Usage: `std::to_string(client->getRemotePort())`

---

## Event Types (AwsEventType enum)

### WebSocket Events
- **WS_EVT_CONNECT** - Client connected to WebSocket
  - `arg` parameter: `nullptr`
  - Action: Create WSChannel, send client ID notifications

- **WS_EVT_DISCONNECT** - Client disconnected from WebSocket
  - `arg` parameter: `nullptr`
  - Action: Remove channel, cleanup

- **WS_EVT_DATA** - Data received from client
  - `arg` parameter: `AwsFrameInfo*` (frame metadata)
  - `data` parameter: Payload bytes
  - `len` parameter: Payload length
  - Frame info includes `opcode` field (WS_TEXT or WS_BINARY)

- **WS_EVT_ERROR** - WebSocket error
  - `arg` parameter: `nullptr`
  - Action: Remove channel, log error

---

## Data Structures

### AwsFrameInfo (for WS_EVT_DATA)
- **opcode** - Frame type identifier
  - Values: `WS_TEXT` (text frame), `WS_BINARY` (binary frame)
  - Other opcodes possible for control frames

### AsyncWebServerResponse
- Container for response data before sending
- Supports streaming via callback
- Supports header/cookie injection

### AsyncResponseStream
- Output stream for progressive JSON/text response
- Methods: `print(const char*)`, `setCode(int)`, `addHeader(name, value)`
- Used for unfinished-length responses

### AsyncWebHeader
- Wrapper for single header value
- Method: `value()` - returns header value as string

### AsyncWebParameter
- Wrapper for request parameter
- Method: `value()` - returns parameter value; chainable `.toInt()`

---

## HTTP Methods & Constants

### HTTP Method Constants
- `HTTP_ANY` - Accept any HTTP method
- `HTTP_GET` - GET request
- `HTTP_POST` - POST request
- `HTTP_PUT` - PUT request
- `HTTP_DELETE` - DELETE request
- `HTTP_HEAD` - HEAD request
- `HTTP_OPTIONS` - OPTIONS request
- `HTTP_PATCH` - PATCH request (may depend on configuration)

### WebSocket Frame Opcodes
- `WS_TEXT` - Text frame (UTF-8 text)
- `WS_BINARY` - Binary frame (raw bytes)
- Additional control frames (continuation, close, ping, pong) not directly used in this codebase

---

## Middleware & Functional Types

### ArMiddlewareNext
- Callable type for middleware continuation
- Conventional signature: `std::function<void()>`
- Must be called to continue middleware chain processing
- After calling `next()`, can continue with post-processing if needed

### AwsGetCurrentBuffer
- Callback type for streaming responses
- Signature: `size_t(uint8_t* buffer, size_t maxLen, size_t total)`
- Returns: Number of bytes written to buffer
- Parameters:
  - `buffer` - Memory to write into
  - `maxLen` - Maximum bytes allowed
  - `total` - Total bytes sent so far (can resume tracking)
- Return `0` to signal end of stream

---

## Usage Patterns

### Route Handler Registration Pattern
```cpp
_webserver->on(path, HTTP_METHOD, callbackFunction);
_webserver->on(path, HTTP_METHOD, callbackFunction, uploadCallbackFunction);
```

### WebSocket Connection Pattern
```cpp
_socket_server->addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
    // Extract session info
    next();  // Allow connection
});

_socket_server->onEvent(
    [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        // Handle: WS_EVT_CONNECT, WS_EVT_DISCONNECT, WS_EVT_DATA, WS_EVT_ERROR
    });
```

### Streaming Response Pattern
```cpp
AsyncWebServerResponse* response = request->beginResponse(
    contentType, fileSize, 
    [](uint8_t* buffer, size_t maxLen, size_t total) -> size_t {
        // Read up to maxLen bytes
        // Return bytes actually read
        return bytesRead;
    });
response->addHeader("Header-Name", "value");
request->send(response);
```

### JSON Streaming Response Pattern
```cpp
AsyncResponseStream* response = request->beginResponseStream("application/json");
response->setCode(200);
response->addHeader("Cache-Control", "no-cache");
// Use response->print() to write JSON incrementally
request->send(response);
```

### WebSocket Messaging Pattern
```cpp
// Send text to specific client
_server->text(clientNum, "message");

// Send binary to specific client  
_server->binary(clientNum, (const uint8_t*)data, length);

// Broadcast to all
_server->textAll("broadcast message");
```

### Client Queue Monitoring Pattern
```cpp
if (_server->client(clientNum)) {
    if (_server->client(clientNum)->queueLen() >= threshold) {
        // Wait or handle backpressure
    }
    if (_server->client(clientNum)->queueIsFull()) {
        // Queue overflow handling
    }
}
```

---

## Summary Statistics

- **AsyncWebServer methods used**: 11 (begin, on, onNotFound, addHandler, addMiddlewares, hasArg, arg, hasHeader, header, sendHeader, client)
- **AsyncWebServerRequest methods used**: 15 (method, url, client, hasParam, getParam, hasHeader, getHeader, beginResponse, beginChunkedResponse, beginResponseStream, send, addHeader [on response], redirect, onDisconnect, setCode [on response])
- **AsyncWebSocket methods used**: 6 (addMiddleware, onEvent, client, cleanupClients, textAll, url)
- **AsyncWebSocketClient methods used**: 7 (id, remoteIP, setCloseClientOnQueueFull, text, binary, queueLen, queueIsFull)
- **Event types used**: 4 (WS_EVT_CONNECT, WS_EVT_DISCONNECT, WS_EVT_DATA, WS_EVT_ERROR)
- **WebSocket opcodes used**: 2 (WS_TEXT, WS_BINARY)
- **HTTP methods used**: 1 (HTTP_ANY commonly, HTTP_GET explicitly checked)

---

## Library Information

- **Library**: ESPAsyncWebServer (custom fork with WebDAV extensions)
- **Includes**: `<ESPAsyncWebServer.h>`
- **Namespace**: Root level (no namespace wrapping in visible API)
- **Build Context**: ESP32 with AsyncTCP support
- **Notes**: Custom fork includes WebDAV handler support not in standard library
