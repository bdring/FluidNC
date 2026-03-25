#include "AsyncTCP.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>

#include <chrono>

/////////////////////////////////////////////////
// AsyncClient Implementation
/////////////////////////////////////////////////

AsyncClient::AsyncClient() : _socket(-1) {
    _rx_last_packet = millis();
}

AsyncClient::~AsyncClient() {
    if (_socket >= 0) {
        _close();
    }
}

bool AsyncClient::connect(IPAddress ip, uint16_t port) {
    if (_socket >= 0) {
        return false;  // Already connected or connecting
    }

    _remote_addr = (uint32_t)ip;
    _remote_port = port;
    _tcp_state   = SYN_SENT;

    if (!_connect_socket()) {
        _tcp_state = CLOSED;
        return false;
    }

    return true;
}

bool AsyncClient::connect(const char* host, uint16_t port) {
    if (_socket >= 0) {
        return false;  // Already connected or connecting
    }

    _connect_host = host;
    _connect_port = port;

    // Blocking DNS lookup (acceptable for prototype)
    struct addrinfo hints = { 0 };
    hints.ai_family       = AF_INET;
    hints.ai_socktype     = SOCK_STREAM;
    hints.ai_protocol     = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    int              ret    = getaddrinfo(host, nullptr, &hints, &result);

    if (ret != 0) {
        if (_error_cb) {
            _error_cb(_error_cb_arg, this, -55);  // DNS failed
        }
        return false;
    }

    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    IPAddress           ip(addr->sin_addr.s_addr);
    freeaddrinfo(result);

    return connect(ip, port);
}

bool AsyncClient::_connect_socket() {
    _socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        return false;
    }

    // Set non-blocking
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);

    // Disable Nagle's algorithm by default
    int yes = 1;
    setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    // Initiate connection
    struct sockaddr_in server_addr;
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = _remote_addr;
    server_addr.sin_port        = htons(_remote_port);

    if (::connect(_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            int err = errno;
            close(_socket);
            _socket    = -1;
            _tcp_state = CLOSED;
            return false;
        }
    }

    _update_socket_addresses();
    _pcb_busy    = true;
    _pcb_sent_at = millis();

    return true;
}

void AsyncClient::_update_socket_addresses() {
    if (_socket < 0) {
        return;
    }

    struct sockaddr_in local_addr;
    socklen_t          local_addr_len = sizeof(local_addr);
    if (getsockname(_socket, (struct sockaddr*)&local_addr, &local_addr_len) == 0) {
        _local_addr = local_addr.sin_addr.s_addr;
        _local_port = ntohs(local_addr.sin_port);
    }

    struct sockaddr_in remote_addr;
    socklen_t          remote_addr_len = sizeof(remote_addr);
    if (getpeername(_socket, (struct sockaddr*)&remote_addr, &remote_addr_len) == 0) {
        _remote_addr = remote_addr.sin_addr.s_addr;
        _remote_port = ntohs(remote_addr.sin_port);
    }
}

void AsyncClient::close(bool now) {
    if (now) {
        _close();
    } else {
        _close_pcb = true;
    }
}

void AsyncClient::stop() {
    close(false);
}

void AsyncClient::abort() {
    _close_error = ERR_ABRT;
    _close();
}

void AsyncClient::_close() {
    if (_socket >= 0) {
        ::close(_socket);
        _socket = -1;
    }

    _tcp_state = CLOSED;
    _pcb_busy  = false;
    _close_pcb = false;

    if (_discard_cb) {
        _discard_cb(_discard_cb_arg, this);
    }
}

void AsyncClient::_error(err_t err) {
    _close_error = err;
    _tcp_state   = CLOSED;

    if (_socket >= 0) {
        ::close(_socket);
        _socket = -1;
    }

    if (_error_cb) {
        _error_cb(_error_cb_arg, this, err);
    }

    if (_discard_cb) {
        _discard_cb(_discard_cb_arg, this);
    }
}

bool AsyncClient::free() {
    if (_socket < 0) {
        return true;
    }

    return (_tcp_state == CLOSED) || (_tcp_state > ESTABLISHED);
}

bool AsyncClient::connected() const {
    return (_socket >= 0) && (_tcp_state == ESTABLISHED);
}

bool AsyncClient::connecting() const {
    return (_socket >= 0) && (_tcp_state > CLOSED) && (_tcp_state < ESTABLISHED);
}

bool AsyncClient::disconnecting() const {
    return (_socket >= 0) && (_tcp_state > ESTABLISHED) && (_tcp_state < TIME_WAIT);
}

bool AsyncClient::disconnected() const {
    return (_tcp_state == CLOSED) || (_tcp_state == TIME_WAIT);
}

uint8_t AsyncClient::state() const {
    return (uint8_t)_tcp_state;
}

bool AsyncClient::canSend() const {
    return !_pcb_busy && (space() > 0);
}

size_t AsyncClient::space() const {
    if ((_socket < 0) || (_tcp_state != ESTABLISHED)) {
        return 0;
    }

    // For prototype: assume reasonable TCP window (typical 64KB)
    // In production, could use TCP_INFO on Linux
    return 65536;
}

size_t AsyncClient::add(const char* data, size_t size, uint8_t apiflags) {
    if (!data || size == 0 || _socket < 0) {
        return 0;
    }

    size_t room = space();

    if (!room) {
        return 0;
    }

    size_t will_send = (room < size) ? room : size;

    {
        std::lock_guard<std::mutex> lock(_send_buffer_lock);
        // Use iterator-based constructor for binary-safe string construction (handles null bytes)
        _send_buffer.push(std::string(data, data + will_send));
        _tx_unsent_len += will_send;
    }

    return will_send;
}

size_t AsyncClient::write(const char* data) {
    if (!data) {
        return 0;
    }
    return write(data, strlen(data));
}

size_t AsyncClient::write(const char* data, size_t size, uint8_t apiflags) {
    size_t added = add(data, size, apiflags);
    if (!added || !send()) {
        return 0;
    }
    return added;
}

bool AsyncClient::send() {
    if (_socket < 0 || _tcp_state != ESTABLISHED) {
        return false;
    }

    int sent = _send_from_buffer();
    if (sent < 0) {
        _error(errno);
        return false;
    }

    _pcb_busy    = true;
    _pcb_sent_at = millis();

    // If we drained the buffer synchronously, mark the bytes as pending-ack so
    // _handle_writable() can fire _sent_cb even when _send_from_buffer() returns 0.
    if (sent > 0) {
        _pending_ack_len += sent;
    }

    return true;
}

int AsyncClient::_send_from_buffer() {
    std::lock_guard<std::mutex> lock(_send_buffer_lock);

    int total_sent = 0;

    while (!_send_buffer.empty()) {
        std::string& msg  = _send_buffer.front();
        int          sent = ::send(_socket, msg.data(), msg.length(), MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Socket full, try again later
            }
            return -1;  // Error
        }

        total_sent += sent;

        if (sent >= (int)msg.length()) {
            size_t msg_len = msg.length();
            _send_buffer.pop();
            _tx_unacked_len += sent;
            _tx_unsent_len -= std::min(_tx_unsent_len, (uint32_t)msg_len);
        } else {
            msg = msg.substr(sent);
            _tx_unacked_len += sent;
            _tx_unsent_len -= std::min(_tx_unsent_len, (uint32_t)sent);
            break;  // Partial send, try rest later
        }
    }

    return total_sent;
}

size_t AsyncClient::ack(size_t len)
{
  if (len > _rx_ack_len) {
    len = _rx_ack_len;
  }
  _rx_ack_len -= len;
  return len;
}

void AsyncClient::ackLater()
{
  _ack_pcb = false;
}

void AsyncClient::setRxTimeout(uint32_t timeout)
{
  _rx_since_timeout = timeout;
}

uint32_t AsyncClient::getRxTimeout() const
{
  return _rx_since_timeout;
}

void AsyncClient::setAckTimeout(uint32_t timeout)
{
  _ack_timeout = timeout;
}

uint32_t AsyncClient::getAckTimeout() const
{
  return _ack_timeout;
}

void AsyncClient::setNoDelay(bool nodelay)
{
  if (_socket < 0) {
    return;
  }

  int yes = nodelay ? 1 : 0;
  setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

bool AsyncClient::getNoDelay() const {
    if (_socket < 0) {
        return false;
    }

    int       yes = 0;
    socklen_t len = sizeof(yes);
    getsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &yes, (socklen_t*)&len);

    return yes != 0;
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

void AsyncClient::_handle_readable() {
    if (_socket < 0) {
        return;
    }

    uint8_t buf[4096];
    int     nread = ::recv(_socket, buf, sizeof(buf), MSG_NOSIGNAL);

    if (nread <= 0) {
        if (nread == 0) {
            // Disconnection
            _close();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            _error(errno);
        }
        return;
    }

    _rx_last_packet = millis();

    if (_recv_cb) {
        _recv_cb(_recv_cb_arg, this, buf, nread);
    }

    _rx_ack_len += nread;
}

void AsyncClient::_handle_writable() {
    if (_socket < 0) {
        return;
    }

    // Check if connection is established
    if (_tcp_state == SYN_SENT) {
        // Non-blocking connect completed
        int       error = 0;
        socklen_t len   = sizeof(error);
        getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &len);

        if (error == 0) {
            _tcp_state = ESTABLISHED;
            _update_socket_addresses();
            _pcb_busy = false;

            if (_connect_cb) {
                _connect_cb(_connect_cb_arg, this);
            }
        } else {
            _error(error);
        }
        return;
    }

    if (_tcp_state != ESTABLISHED) {
        return;
    }

    // Send pending data
    int sent = _send_from_buffer();

    if (sent >= 0 && _sent_cb) {
        // Also account for bytes sent synchronously in send() that weren't yet acked.
        size_t total = (sent > 0 ? sent : 0) + _pending_ack_len;
        _pending_ack_len = 0;
        if (total > 0) {
            _sent_cb(_sent_cb_arg, this, total, (millis() - _pcb_sent_at));
        }
    } else {
        _pending_ack_len = 0;
    }

    _pcb_busy = (_tx_unsent_len > 0) || (!_send_buffer.empty());
}

void AsyncClient::_handle_error() {
    if (_socket < 0) {
        return;
    }

    int       error = 0;
    socklen_t len   = sizeof(error);
    getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &len);

    if (error != 0) {
        _error(error);
    }
}

void AsyncClient::_poll() {
    if (_socket < 0) {
        return;
    }

    // Close requested
    if (_close_pcb) {
        _close_pcb = false;
        _close();
        return;
    }

    uint32_t now = millis();

    // ACK Timeout
    if (_pcb_busy && _ack_timeout && (now - _pcb_sent_at) >= _ack_timeout) {
        _pcb_busy = false;
        if (_timeout_cb) {
            _timeout_cb(_timeout_cb_arg, this, (now - _pcb_sent_at));
        }
        return;
    }

    // RX Timeout
    if (_rx_since_timeout && (now - _rx_last_packet) >= (_rx_since_timeout * 1000)) {
        _close();
        return;
    }

    // Everything is fine
    if (_poll_cb) {
        _poll_cb(_poll_cb_arg, this);
    }
}

const char* AsyncClient::errorToString(err_t error) const {
    return strerror(error);
}

const char* AsyncClient::stateToString() const {
    switch (_tcp_state) {
        case CLOSED:
            return "Closed";
        case LISTEN:
            return "Listen";
        case SYN_SENT:
            return "SYN Sent";
        case SYN_RCVD:
            return "SYN Received";
        case ESTABLISHED:
            return "Established";
        case FIN_WAIT_1:
            return "FIN Wait 1";
        case FIN_WAIT_2:
            return "FIN Wait 2";
        case CLOSE_WAIT:
            return "Close Wait";
        case CLOSING:
            return "Closing";
        case LAST_ACK:
            return "Last ACK";
        case TIME_WAIT:
            return "Time Wait";
        default:
            return "UNKNOWN";
    }
}

/////////////////////////////////////////////////
// AsyncServer Implementation
/////////////////////////////////////////////////

AsyncServer::AsyncServer(IPAddress addr, uint16_t port) : _port(port), _addr(addr), _noDelay(false), _listen_socket(-1) {}

AsyncServer::AsyncServer(uint16_t port) : _port(port), _addr(IPADDR_ANY), _noDelay(false), _listen_socket(-1) {}

AsyncServer::~AsyncServer() {
    end();
}

void AsyncServer::onClient(AcConnectHandler cb, void* arg) {
    _connect_cb     = cb;
    _connect_cb_arg = arg;
}

void AsyncServer::begin() {
    if (_listen_socket >= 0) {
        return;  // Already listening
    }

    _listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_listen_socket < 0) {
        return;
    }

    // Set non-blocking
    int flags = fcntl(_listen_socket, F_GETFL, 0);
    fcntl(_listen_socket, F_SETFL, flags | O_NONBLOCK);

    // Allow address reuse
    int yes = 1;
    setsockopt(_listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind
    struct sockaddr_in local_addr;
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = (uint32_t)_addr;
    local_addr.sin_port        = htons(_port);

    if (bind(_listen_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        ::fprintf(stderr, "** Cannot bind to socket\n");
        ::close(_listen_socket);
        _listen_socket = -1;
        return;
    }

    // Listen
    if (listen(_listen_socket, SOMAXCONN) < 0) {
        ::close(_listen_socket);
        _listen_socket = -1;
        return;
    }

    // Register with polling manager
    PosixAsyncTCPManager::getInstance().registerServer(this);
}

void AsyncServer::end() {
    if (_listen_socket >= 0) {
        PosixAsyncTCPManager::getInstance().unregisterServer(this);
        ::close(_listen_socket);
        _listen_socket = -1;
    }
}

uint8_t AsyncServer::status() const {
    if (_listen_socket < 0) {
        return CLOSED;
    }
    return LISTEN;
}

void AsyncServer::_handle_accept() {
    if (_listen_socket < 0 || !_connect_cb) {
        return;
    }

    struct sockaddr_in client_addr;
    socklen_t          client_addr_len = sizeof(client_addr);

    int client_socket = accept(_listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

    if (client_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error
        }
        return;
    }

    // Create AsyncClient for accepted connection
    auto client          = std::make_shared<AsyncClient>();
    client->_socket      = client_socket;
    client->_tcp_state   = ESTABLISHED;
    client->_remote_addr = client_addr.sin_addr.s_addr;
    client->_remote_port = ntohs(client_addr.sin_port);

    // Set non-blocking
    int flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

    // Set TCP_NODELAY if configured
    int yes = _noDelay ? 1 : 0;
    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    client->_update_socket_addresses();
    client->_rx_last_packet = millis();

    // Register with manager
    PosixAsyncTCPManager::getInstance().registerClient(client);

    // Call the accept callback
    _connect_cb(_connect_cb_arg, client.get());
}

/////////////////////////////////////////////////
// PosixAsyncTCPManager Implementation
/////////////////////////////////////////////////

PosixAsyncTCPManager& PosixAsyncTCPManager::getInstance() {
    static PosixAsyncTCPManager instance;
    return instance;
}

PosixAsyncTCPManager::~PosixAsyncTCPManager() {
    end();
}

void PosixAsyncTCPManager::registerServer(AsyncServer* server) {
    std::lock_guard<std::mutex> lock(_servers_lock);
    _servers.push_back(server);
}

void PosixAsyncTCPManager::unregisterServer(AsyncServer* server) {
    std::lock_guard<std::mutex> lock(_servers_lock);
    auto it = std::find(_servers.begin(), _servers.end(), server);
    if (it != _servers.end()) {
        _servers.erase(it);
    }
}

void PosixAsyncTCPManager::registerClient(std::shared_ptr<AsyncClient> client) {
    std::lock_guard<std::mutex> lock(_clients_lock);
    _clients.push_back(client);
}

void PosixAsyncTCPManager::unregisterClient(std::shared_ptr<AsyncClient> client) {
    std::lock_guard<std::mutex> lock(_clients_lock);
    auto it = std::find(_clients.begin(), _clients.end(), client);
    if (it != _clients.end()) {
        _clients.erase(it);
    }
}

void PosixAsyncTCPManager::begin() {
    if (_running) {
        return;
    }

    _running     = true;
    _poll_thread = std::thread(&PosixAsyncTCPManager::_poll_thread_main, this);
}

void PosixAsyncTCPManager::end() {
    if (!_running) {
        return;
    }

    _running = false;
    if (_poll_thread.joinable()) {
        _poll_thread.join();
    }
}

void PosixAsyncTCPManager::_poll_thread_main() {
    while (_running) {
        fd_set readfds, writefds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        int maxfd = 0;

        // Add listen sockets
        {
            std::lock_guard<std::mutex> lock(_servers_lock);
            for (auto server : _servers) {
                if (server->_listen_socket >= 0) {
                    FD_SET(server->_listen_socket, &readfds);
                    maxfd = std::max(maxfd, server->_listen_socket);
                }
            }
        }

        // Add client sockets
        {
            std::lock_guard<std::mutex> lock(_clients_lock);
            for (auto it = _clients.begin(); it != _clients.end();) {
                auto client = *it;
                if (client->_socket >= 0) {
                    FD_SET(client->_socket, &readfds);
                    FD_SET(client->_socket, &writefds);
                    FD_SET(client->_socket, &exceptfds);
                    maxfd = std::max(maxfd, client->_socket);
                    ++it;
                } else {
                    // Stale client, remove it
                    it = _clients.erase(it);
                }
            }
        }

        // Use 125ms timeout (same as lwip poll interval)
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 125000;

        int ret = select(maxfd + 1, &readfds, &writefds, &exceptfds, &tv);

        if (ret < 0) {
            if (errno != EINTR) {
                // Error in select
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }
        
        // Handle listen sockets
        {
            std::lock_guard<std::mutex> lock(_servers_lock);
            for (auto server : _servers) {
                if (server->_listen_socket >= 0 && FD_ISSET(server->_listen_socket, &readfds)) {
                    server->_handle_accept();
                }
            }
        }

        // Handle client sockets
        {
            std::lock_guard<std::mutex> lock(_clients_lock);
            for (auto client : _clients) {
                if (client->_socket >= 0) {
                    if (FD_ISSET(client->_socket, &readfds)) {
                        client->_handle_readable();
                    }
                    if (FD_ISSET(client->_socket, &writefds)) {
                        client->_handle_writable();
                    }
                    if (FD_ISSET(client->_socket, &exceptfds)) {
                        client->_handle_error();
                    }
                }
            }
        }

        // Periodic polling for all clients (every 125ms)
        {
            std::lock_guard<std::mutex> lock(_clients_lock);
            for (auto client : _clients) {
                client->_poll();
            }
        }
    }
}
