#ifndef POSIX_ASYNC_TCP_H_
#define POSIX_ASYNC_TCP_H_

#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <cstring>

/** 255.255.255.255 */
#define IPADDR_NONE ((uint32_t)0xffffffffUL)
/** 127.0.0.1 */
#define IPADDR_LOOPBACK ((uint32_t)0x7f000001UL)
/** 0.0.0.0 */
#define IPADDR_ANY ((uint32_t)0x00000000UL)
/** 255.255.255.255 */
#define IPADDR_BROADCAST ((uint32_t)0xffffffffUL)

using err_t = int;

// Common error code mappings (errno values)
#ifndef ERR_OK
  #define ERR_OK          0
  #define ERR_MEM         ENOMEM
  #define ERR_BUF         ENOBUFS
  #define ERR_TIMEOUT     ETIMEDOUT
  #define ERR_RTE         ENETUNREACH
  #define ERR_INPROGRESS  EINPROGRESS
  #define ERR_VAL         EINVAL
  #define ERR_WOULDBLOCK  EWOULDBLOCK
  #define ERR_USE         EADDRINUSE
  #define ERR_ALREADY     EALREADY
  #define ERR_CONN        ENOTCONN
  #define ERR_IF          EIO
  #define ERR_ABRT        ECONNABORTED
  #define ERR_RST         ECONNRESET
  #define ERR_CLSD        ECONNREFUSED
  #define ERR_ARG         EINVAL
#endif

/////////////////////////////////////////////////

class AsyncClient;
class AsyncServer;

#define ASYNC_MAX_ACK_TIME      5000
#define ASYNC_WRITE_FLAG_COPY   0x01
#define ASYNC_WRITE_FLAG_MORE   0x02
#define TCP_MSS                 1460

/////////////////////////////////////////////////

// Callback type definitions
typedef std::function<void(void*, AsyncClient*)> AcConnectHandler;
typedef std::function<void(void*, AsyncClient*, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void*, AsyncClient*, err_t error)> AcErrorHandler;
typedef std::function<void(void*, AsyncClient*, void *data, size_t len)> AcDataHandler;
typedef std::function<void(void*, AsyncClient*, uint32_t time)> AcTimeoutHandler;
typedef std::function<void(void*, AsyncClient*)> AcPollHandler;

/////////////////////////////////////////////////

enum tcp_state
{
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};

/////////////////////////////////////////////////

class AsyncClient
{
  protected:
    int _socket = -1;

    AcConnectHandler _connect_cb = nullptr;
    void* _connect_cb_arg = nullptr;

    AcConnectHandler _discard_cb = nullptr;
    void* _discard_cb_arg = nullptr;

    AcAckHandler _sent_cb = nullptr;
    void* _sent_cb_arg = nullptr;

    AcErrorHandler _error_cb = nullptr;
    void* _error_cb_arg = nullptr;

    AcDataHandler _recv_cb = nullptr;
    void* _recv_cb_arg = nullptr;

    AcTimeoutHandler _timeout_cb = nullptr;
    void* _timeout_cb_arg = nullptr;

    AcPollHandler _poll_cb = nullptr;
    void* _poll_cb_arg = nullptr;

    // State tracking
    tcp_state _tcp_state = CLOSED;
    uint32_t  _pcb_sent_at = 0;
    bool      _close_pcb = false;
    bool      _ack_pcb = true;
    bool      _pcb_busy = false;

    // Tx tracking
    uint32_t _tx_unacked_len = 0;
    uint32_t _tx_acked_len = 0;
    uint32_t _tx_unsent_len = 0;
    size_t   _pending_ack_len = 0;  // bytes sent synchronously in send(), awaiting _handle_writable() ack
    std::queue<std::string> _send_buffer;
    std::mutex _send_buffer_lock;

    // Rx tracking
    uint32_t _rx_ack_len = 0;
    uint32_t _rx_last_packet = 0;
    uint32_t _rx_since_timeout = 0;
    uint32_t _ack_timeout = ASYNC_MAX_ACK_TIME;

    // Connection tracking
    uint16_t  _connect_port = 0;
    std::string _connect_host;
    err_t _close_error = ERR_OK;

    // IPAddress and port tracking
    uint32_t _remote_addr = 0;
    uint16_t _remote_port = 0;
    uint32_t _local_addr = 0;
    uint16_t _local_port = 0;

    // Internal state management
    void _close();
    void _error(err_t err);
    bool _connect_socket();
    void _update_socket_addresses();
    int _send_from_buffer();

    // Called by polling thread
    friend class AsyncServer;
    friend class PosixAsyncTCPManager;

    void _handle_readable();
    void _handle_writable();
    void _handle_error();
    void _poll();

  public:
    AsyncClient* prev = nullptr;
    AsyncClient* next = nullptr;

    AsyncClient();
    ~AsyncClient();

    // Connection management
    bool connect(IPAddress ip, uint16_t port);
    bool connect(const char* host, uint16_t port);
    void close(bool now = false);
    void stop();
    void abort();
    bool free();

    // Send data
    size_t add(const char* data, size_t size, uint8_t apiflags = 0);
    size_t write(const char* data);
    size_t write(const char* data, size_t size, uint8_t apiflags = 0);
    bool send();

    // Receive acknowledgment
    size_t ack(size_t len);
    void ackLater();

    // State inquiry
    uint8_t state() const;
    bool connecting() const;
    bool connected() const;
    bool disconnecting() const;
    bool disconnected() const;
    bool freeable() const;
    bool canSend() const;

    // Socket options
    void setRxTimeout(uint32_t timeout);      // seconds
    uint32_t getRxTimeout() const;
    void setAckTimeout(uint32_t timeout);     // milliseconds
    uint32_t getAckTimeout() const;
    void setNoDelay(bool nodelay);
    bool getNoDelay() const;

    // Address information
    uint16_t getMss() const { return TCP_MSS; }
    uint32_t getRemoteAddress() const { return _remote_addr; }
    uint16_t getRemotePort() const { return _remote_port; }
    uint32_t getLocalAddress() const { return _local_addr; }
    uint16_t getLocalPort() const { return _local_port; }

    IPAddress remoteIP() const;
    uint16_t remotePort() const;
    IPAddress localIP() const;
    uint16_t localPort() const;

    // Buffer space inquiry
    size_t space() const;

    // Callbacks
    void onConnect(AcConnectHandler cb, void* arg = nullptr);
    void onDisconnect(AcConnectHandler cb, void* arg = nullptr);
    void onAck(AcAckHandler cb, void* arg = nullptr);
    void onError(AcErrorHandler cb, void* arg = nullptr);
    void onData(AcDataHandler cb, void* arg = nullptr);
    void onTimeout(AcTimeoutHandler cb, void* arg = nullptr);
    void onPoll(AcPollHandler cb, void* arg = nullptr);

    // Error string
    const char * errorToString(err_t error) const;
    const char * stateToString() const;
};

/////////////////////////////////////////////////

class AsyncServer
{
  protected:
    uint16_t _port = 0;
    IPAddress _addr = IPADDR_ANY;
    bool _noDelay = false;
    int _listen_socket = -1;

    AcConnectHandler _connect_cb = nullptr;
    void* _connect_cb_arg = nullptr;

    err_t _accept(int client_socket, struct sockaddr_in* client_addr);
    static err_t _s_accept_wrapper(AsyncServer* server, int client_socket, struct sockaddr_in* client_addr);

    friend class PosixAsyncTCPManager;
    void _handle_accept();

  public:
    AsyncServer(IPAddress addr, uint16_t port);
    AsyncServer(uint16_t port);
    ~AsyncServer();

    void onClient(AcConnectHandler cb, void* arg);

    void begin();
    void end();

    void setNoDelay(bool nodelay) { _noDelay = nodelay; }
    bool getNoDelay() const { return _noDelay; }

    uint8_t status() const;
};

/////////////////////////////////////////////////

// Global manager for polling thread
class PosixAsyncTCPManager
{
  public:
    static PosixAsyncTCPManager& getInstance();

    void registerServer(AsyncServer* server);
    void unregisterServer(AsyncServer* server);

    void registerClient(std::shared_ptr<AsyncClient> client);
    void unregisterClient(std::shared_ptr<AsyncClient> client);

    void begin();
    void end();

    bool isRunning() const { return _running; }

  private:
    PosixAsyncTCPManager() = default;
    ~PosixAsyncTCPManager();

    std::vector<AsyncServer*> _servers;
    std::vector<std::shared_ptr<AsyncClient>> _clients;
    std::mutex _clients_lock;
    std::mutex _servers_lock;

    std::thread _poll_thread;
    bool _running = false;

    void _poll_thread_main();
};

#endif  // POSIX_ASYNC_TCP_H_
