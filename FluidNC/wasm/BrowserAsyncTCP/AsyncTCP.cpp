// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// BrowserAsyncTCP -- see AsyncTCP.h for the model and the JS interface.

#include "AsyncTCP.h"

#include <emscripten.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <map>
#include <thread>
#include <vector>

// Window offered by space(): how many unflushed bytes a connection may hold.
// Flushing to JS is immediate, so this mainly bounds a burst.
static constexpr size_t   tx_window       = 16 * 1024;
static constexpr uint32_t poll_interval_ms = 125;  // retry cadence for responses waiting on data

// Output to the page.  Runs on the async_tcp thread; MAIN_THREAD_EM_ASM
// proxies to the main thread, where `self` is the page (see wasm/Console.cpp).
// The heap view may be stale after memory growth under pthreads, hence
// growMemViews() before slicing.
static void js_vconn_data(int id, const char* data, size_t len) {
    MAIN_THREAD_EM_ASM(
        {
            if (typeof growMemViews === 'function') {
                growMemViews();
            }
            const bytes = HEAPU8.slice($1, $1 + $2);
            if (typeof self.fluidncOnVconnData === 'function') {
                self.fluidncOnVconnData($0, bytes);
            }
        },
        id,
        data,
        len);
}

static void js_vconn_close(int id) {
    MAIN_THREAD_EM_ASM(
        {
            if (typeof self.fluidncOnVconnClose === 'function') {
                self.fluidncOnVconnClose($0);
            }
        },
        id);
}

// The connection table and the async_tcp thread.
class BrowserAsyncTCP {
public:
    static BrowserAsyncTCP& instance() {
        static BrowserAsyncTCP inst;
        return inst;
    }

    // ---- called from JS (main thread) via the exports below ----

    int open(uint16_t port) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!find_server(port)) {
            return 0;
        }
        int id = _next_id++;
        if (_next_id <= 0) {
            _next_id = 1;
        }
        _events.push_back({ Event::Open, id, port, {} });
        _cv.notify_one();
        return id;
    }
    void send(int id, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(_mutex);
        _events.push_back({ Event::Data, id, 0, std::string(reinterpret_cast<const char*>(data), len) });
        _cv.notify_one();
    }
    void close(int id) {
        std::lock_guard<std::mutex> lock(_mutex);
        _events.push_back({ Event::Close, id, 0, {} });
        _cv.notify_one();
    }

    // ---- called from AsyncServer/AsyncClient ----

    void add_server(AsyncServer* server) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (std::find(_servers.begin(), _servers.end(), server) == _servers.end()) {
                _servers.push_back(server);
            }
        }
        start();
    }
    void remove_server(AsyncServer* server) {
        std::lock_guard<std::mutex> lock(_mutex);
        _servers.erase(std::remove(_servers.begin(), _servers.end(), server), _servers.end());
    }
    void remove_client(int id) {
        std::lock_guard<std::mutex> lock(_mutex);
        _clients.erase(id);
    }
    void wake() {
        std::lock_guard<std::mutex> lock(_mutex);
        _wake = true;
        _cv.notify_one();
    }
    bool on_async_thread() const { return std::this_thread::get_id() == _thread_id; }

    // A client is live while its id is in the table; _close() and the
    // destructor remove it.  Callers re-check this after any callback, which
    // may have closed or deleted the client.
    bool is_live(int id) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _clients.count(id) != 0;
    }

private:
    struct Event {
        enum Type { Open, Data, Close } type;
        int         id;
        uint16_t    port;
        std::string data;
    };

    std::mutex                  _mutex;
    std::condition_variable     _cv;
    std::deque<Event>           _events;
    std::vector<AsyncServer*>   _servers;
    std::map<int, AsyncClient*> _clients;
    int                         _next_id = 1;
    bool                        _wake    = false;
    bool                        _running = false;
    std::atomic<std::thread::id> _thread_id;

    // Caller holds _mutex.
    AsyncServer* find_server(uint16_t port) {
        for (auto s : _servers) {
            if (s->_port == port && s->_listening) {
                return s;
            }
        }
        return nullptr;
    }

    AsyncClient* lookup(int id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto                        it = _clients.find(id);
        return it == _clients.end() ? nullptr : it->second;
    }

    void start() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_running) {
            return;
        }
        _running = true;
        std::thread t([this] { run(); });
        _thread_id = t.get_id();
        t.detach();
    }

    void run() {
        uint32_t next_poll = millis() + poll_interval_ms;
        while (true) {
            std::deque<Event> events;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait_for(lock, std::chrono::milliseconds(poll_interval_ms), [this] { return !_events.empty() || _wake; });
                events.swap(_events);
                _wake = false;
            }
            for (auto& ev : events) {
                handle(ev);
            }

            bool poll_due = int32_t(millis() - next_poll) >= 0;
            if (poll_due) {
                next_poll = millis() + poll_interval_ms;
            }

            std::vector<int> ids;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                for (auto const& [id, c] : _clients) {
                    ids.push_back(id);
                }
            }
            for (int id : ids) {
                if (auto c = lookup(id)) {
                    c->_service(poll_due);
                }
            }
        }
    }

    void handle(Event& ev) {
        switch (ev.type) {
            case Event::Open: {
                AsyncServer* server;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    server = find_server(ev.port);
                }
                if (!server || !server->_connect_cb) {
                    js_vconn_close(ev.id);
                    return;
                }
                auto c             = new AsyncClient();
                c->_id             = ev.id;
                c->_tcp_state      = ESTABLISHED;
                c->_remote_addr    = IPADDR_LOOPBACK;
                c->_remote_port    = uint16_t(ev.id);  // distinct per connection, like an ephemeral port
                c->_local_addr     = IPADDR_LOOPBACK;
                c->_local_port     = ev.port;
                c->_rx_last_packet = millis();
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _clients[ev.id] = c;
                }
                server->_connect_cb(server->_connect_cb_arg, c);  // may delete c on failure
            } break;
            case Event::Data: {
                AsyncClient* c = lookup(ev.id);
                if (!c || c->_tcp_state != ESTABLISHED) {
                    return;
                }
                c->_rx_last_packet = millis();
                c->_rx_ack_len += ev.data.length();
                if (c->_recv_cb) {
                    c->_recv_cb(c->_recv_cb_arg, c, ev.data.data(), ev.data.length());
                }
            } break;
            case Event::Close: {
                if (AsyncClient* c = lookup(ev.id)) {
                    c->_peer_closed = true;
                    c->_close();
                }
            } break;
        }
    }
};

/////////////////////////////////////////////////
// AsyncClient
/////////////////////////////////////////////////

AsyncClient::AsyncClient() {}

AsyncClient::~AsyncClient() {
    if (_id) {
        BrowserAsyncTCP::instance().remove_client(_id);
    }
}

// Runs on the async_tcp thread.  May destroy *this via the discard callback.
void AsyncClient::_close() {
    if (_tcp_state == CLOSED) {
        return;
    }
    _tcp_state = CLOSED;
    _close_pcb = false;
    _pcb_busy  = false;
    {
        std::lock_guard<std::mutex> lock(_tx_lock);
        _tx.clear();
    }
    int id = _id;
    BrowserAsyncTCP::instance().remove_client(id);
    if (!_peer_closed) {
        js_vconn_close(id);
    }
    if (_discard_cb) {
        _discard_cb(_discard_cb_arg, this);  // usually deletes this
    }
}

// Runs on the async_tcp thread: flush output, deliver the "ack", run
// deferred closes and timeouts, and the periodic poll.  Returns false if
// *this was destroyed.
bool AsyncClient::_service(bool poll_due) {
    auto& tcp = BrowserAsyncTCP::instance();
    int   id  = _id;

    std::string out;
    {
        std::lock_guard<std::mutex> lock(_tx_lock);
        out.swap(_tx);
    }
    if (!out.empty() && _tcp_state == ESTABLISHED) {
        js_vconn_data(id, out.data(), out.length());
        // JS has the bytes; treat that as the peer's ACK.
        _pcb_busy = false;
        if (_sent_cb) {
            uint32_t elapsed = millis() - _pcb_sent_at;
            _sent_cb(_sent_cb_arg, this, out.length(), elapsed);
        }
    }

    // _sent_cb may have aborted the connection, destroying *this.
    if (!tcp.is_live(id)) {
        return false;
    }

    if (_close_pcb) {
        // Flush anything the close path queued first.
        std::string rest;
        {
            std::lock_guard<std::mutex> lock(_tx_lock);
            rest.swap(_tx);
        }
        if (!rest.empty() && _tcp_state == ESTABLISHED) {
            js_vconn_data(id, rest.data(), rest.length());
        }
        _close();
        return false;
    }

    if (!poll_due || _tcp_state != ESTABLISHED) {
        return true;
    }

    uint32_t now = millis();
    if (_pcb_busy && _ack_timeout && (now - _pcb_sent_at) >= _ack_timeout) {
        _pcb_busy = false;
        if (_timeout_cb) {
            _timeout_cb(_timeout_cb_arg, this, now - _pcb_sent_at);
        }
        return tcp.is_live(id);
    }
    if (_rx_timeout && (now - _rx_last_packet) >= (_rx_timeout * 1000)) {
        _close();
        return false;
    }
    if (_poll_cb) {
        _poll_cb(_poll_cb_arg, this);
    }
    return tcp.is_live(id);
}

bool AsyncClient::connect(IPAddress ip, uint16_t port) {
    return false;  // outbound connections are not supported
}
bool AsyncClient::connect(const char* host, uint16_t port) {
    return false;
}

void AsyncClient::close(bool now) {
    if (_tcp_state == CLOSED) {
        return;
    }
    if (now && BrowserAsyncTCP::instance().on_async_thread()) {
        _close();
        return;
    }
    _close_pcb = true;
    BrowserAsyncTCP::instance().wake();
}
void AsyncClient::stop() {
    close(false);
}
void AsyncClient::abort() {
    close(true);
}
bool AsyncClient::free() {
    return _tcp_state == CLOSED || _tcp_state > ESTABLISHED;
}

size_t AsyncClient::add(const char* data, size_t size, uint8_t apiflags) {
    if (!data || !size || _tcp_state != ESTABLISHED) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(_tx_lock);
    size_t                      room = _tx.length() < tx_window ? tx_window - _tx.length() : 0;
    size_t                      n    = std::min(room, size);
    _tx.append(data, n);
    return n;
}
size_t AsyncClient::write(const char* data) {
    return data ? write(data, strlen(data)) : 0;
}
size_t AsyncClient::write(const char* data, size_t size, uint8_t apiflags) {
    size_t added = add(data, size, apiflags);
    if (!added || !send()) {
        return 0;
    }
    return added;
}
bool AsyncClient::send() {
    if (_tcp_state != ESTABLISHED) {
        return false;
    }
    _pcb_busy    = true;
    _pcb_sent_at = millis();
    BrowserAsyncTCP::instance().wake();
    return true;
}

size_t AsyncClient::ack(size_t len) {
    len = std::min(len, _rx_ack_len);
    _rx_ack_len -= len;
    return len;
}
void AsyncClient::ackLater() {}

uint8_t AsyncClient::state() const {
    return _tcp_state;
}
bool AsyncClient::connecting() const {
    return _tcp_state > CLOSED && _tcp_state < ESTABLISHED;
}
bool AsyncClient::connected() const {
    return _tcp_state == ESTABLISHED;
}
bool AsyncClient::disconnecting() const {
    return _tcp_state > ESTABLISHED && _tcp_state < TIME_WAIT;
}
bool AsyncClient::disconnected() const {
    return _tcp_state == CLOSED || _tcp_state == TIME_WAIT;
}
bool AsyncClient::freeable() const {
    return disconnected();
}
bool AsyncClient::canSend() const {
    return space() > 0;
}

void AsyncClient::setRxTimeout(uint32_t timeout) {
    _rx_timeout = timeout;
}
uint32_t AsyncClient::getRxTimeout() const {
    return _rx_timeout;
}
void AsyncClient::setAckTimeout(uint32_t timeout) {
    _ack_timeout = timeout;
}
uint32_t AsyncClient::getAckTimeout() const {
    return _ack_timeout;
}
void AsyncClient::setNoDelay(bool nodelay) {
    _no_delay = nodelay;
}
bool AsyncClient::getNoDelay() const {
    return _no_delay;
}

IPAddress AsyncClient::remoteIP() const {
    return IPAddress(_remote_addr);
}
uint16_t AsyncClient::remotePort() const {
    return _remote_port;
}
IPAddress AsyncClient::localIP() const {
    return IPAddress(_local_addr);
}
uint16_t AsyncClient::localPort() const {
    return _local_port;
}

size_t AsyncClient::space() const {
    if (_tcp_state != ESTABLISHED) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(_tx_lock);
    return _tx.length() < tx_window ? tx_window - _tx.length() : 0;
}

void AsyncClient::onConnect(AcConnectHandler cb, void* arg) {
    _connect_cb     = cb;
    _connect_cb_arg = arg;
}
void AsyncClient::onDisconnect(AcConnectHandler cb, void* arg) {
    _discard_cb     = cb;
    _discard_cb_arg = arg;
}
void AsyncClient::onAck(AcAckHandler cb, void* arg) {
    _sent_cb     = cb;
    _sent_cb_arg = arg;
}
void AsyncClient::onError(AcErrorHandler cb, void* arg) {
    _error_cb     = cb;
    _error_cb_arg = arg;
}
void AsyncClient::onData(AcDataHandler cb, void* arg) {
    _recv_cb     = cb;
    _recv_cb_arg = arg;
}
void AsyncClient::onTimeout(AcTimeoutHandler cb, void* arg) {
    _timeout_cb     = cb;
    _timeout_cb_arg = arg;
}
void AsyncClient::onPoll(AcPollHandler cb, void* arg) {
    _poll_cb     = cb;
    _poll_cb_arg = arg;
}

const char* AsyncClient::errorToString(err_t error) const {
    return error == ERR_OK ? "OK" : strerror(error);
}
const char* AsyncClient::stateToString() const {
    static const char* names[] = { "Closed",     "Listen",     "SYN Sent", "SYN Received", "Established", "FIN Wait 1",
                                   "FIN Wait 2", "Close Wait", "Closing",  "Last ACK",     "Time Wait" };
    return names[_tcp_state];
}

/////////////////////////////////////////////////
// AsyncServer
/////////////////////////////////////////////////

AsyncServer::AsyncServer(IPAddress addr, uint16_t port) : _port(port), _addr(addr) {}
AsyncServer::AsyncServer(uint16_t port) : _port(port) {}
AsyncServer::~AsyncServer() {
    end();
}

void AsyncServer::onClient(AcConnectHandler cb, void* arg) {
    _connect_cb     = cb;
    _connect_cb_arg = arg;
}
void AsyncServer::begin() {
    _listening = true;
    BrowserAsyncTCP::instance().add_server(this);
}
void AsyncServer::end() {
    _listening = false;
    BrowserAsyncTCP::instance().remove_server(this);
}
uint8_t AsyncServer::status() const {
    return _listening ? LISTEN : CLOSED;
}

/////////////////////////////////////////////////
// JS interface
/////////////////////////////////////////////////

extern "C" {
// Opens a virtual connection to the server listening on `port` (the HTTP
// port, normally 80).  Returns its id, or 0 if nothing listens there yet --
// e.g. before FluidNC has finished starting.
EMSCRIPTEN_KEEPALIVE
int fluidnc_vconn_open(int port) {
    return BrowserAsyncTCP::instance().open(uint16_t(port));
}

// Delivers bytes from the peer.  Binary-safe; call with ccall's 'array'
// argument type (or a malloc'd pointer).
EMSCRIPTEN_KEEPALIVE
void fluidnc_vconn_send(int id, const uint8_t* data, int len) {
    if (data && len > 0) {
        BrowserAsyncTCP::instance().send(id, data, size_t(len));
    }
}

// The peer has closed the connection.
EMSCRIPTEN_KEEPALIVE
void fluidnc_vconn_close(int id) {
    BrowserAsyncTCP::instance().close(id);
}
}
