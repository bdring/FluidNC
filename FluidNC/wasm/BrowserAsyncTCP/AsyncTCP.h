// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// BrowserAsyncTCP: the AsyncTCP API that ESPAsyncWebServer builds on, for the
// wasm port.  A browser page cannot listen on a socket, so "connections" are
// carried from JS instead of BSD sockets: on the demo page, a Service Worker
// relays HTTP and a WebSocket stand-in relays WebSockets (see
// FluidNC/wasm/README.md).
//
// The public interface matches PosixAsyncTCP (and therefore what
// ESPAsyncWebServer expects with HOST defined).  Underneath, a "connection"
// is a numbered virtual connection opened, fed and closed from JS through
// exported functions, with output delivered back through page callbacks:
//
//   JS -> wasm   id = fluidnc_vconn_open(port)       (0 = no server on port)
//                fluidnc_vconn_send(id, bytes, len)
//                fluidnc_vconn_close(id)
//   wasm -> JS   self.fluidncOnVconnData(id, Uint8Array)
//                self.fluidncOnVconnClose(id)
//
// The bytes are whatever would have crossed a TCP socket: raw HTTP/1.1
// requests and responses, or a WebSocket upgrade followed by frames.
//
// All AsyncClient/AsyncServer callbacks run on one "async_tcp" thread, as
// they do on lwIP's task on ESP32, so ESPAsyncWebServer's threading
// assumptions hold.  add()/send()/close() may be called from other threads
// (e.g. WSChannel output from the polling task); they only queue work for
// that thread.  Outbound connect() is not supported.

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <functional>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <atomic>
#include <string>

#define IPADDR_NONE ((uint32_t)0xffffffffUL)
#define IPADDR_LOOPBACK ((uint32_t)0x7f000001UL)
#define IPADDR_ANY ((uint32_t)0x00000000UL)
#define IPADDR_BROADCAST ((uint32_t)0xffffffffUL)

using err_t = int;

#ifndef ERR_OK
#    define ERR_OK 0
#    define ERR_MEM ENOMEM
#    define ERR_BUF ENOBUFS
#    define ERR_TIMEOUT ETIMEDOUT
#    define ERR_RTE ENETUNREACH
#    define ERR_INPROGRESS EINPROGRESS
#    define ERR_VAL EINVAL
#    define ERR_WOULDBLOCK EWOULDBLOCK
#    define ERR_USE EADDRINUSE
#    define ERR_ALREADY EALREADY
#    define ERR_CONN ENOTCONN
#    define ERR_IF EIO
#    define ERR_ABRT ECONNABORTED
#    define ERR_RST ECONNRESET
#    define ERR_CLSD ECONNREFUSED
#    define ERR_ARG EINVAL
#endif

class AsyncClient;
class AsyncServer;

#define ASYNC_MAX_ACK_TIME 5000
#define ASYNC_WRITE_FLAG_COPY 0x01
#define ASYNC_WRITE_FLAG_MORE 0x02
#define TCP_MSS 1460

typedef std::function<void(void*, AsyncClient*)>                            AcConnectHandler;
typedef std::function<void(void*, AsyncClient*, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void*, AsyncClient*, err_t error)>               AcErrorHandler;
typedef std::function<void(void*, AsyncClient*, void* data, size_t len)>    AcDataHandler;
typedef std::function<void(void*, AsyncClient*, uint32_t time)>             AcTimeoutHandler;
typedef std::function<void(void*, AsyncClient*)>                            AcPollHandler;

enum tcp_state {
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

class BrowserAsyncTCP;

class AsyncClient {
protected:
    friend class BrowserAsyncTCP;

    int _id = 0;  // virtual connection number shared with JS; 0 = none

    // Outbound bytes queued by add(), flushed to JS on the async_tcp thread.
    mutable std::mutex _tx_lock;
    std::string        _tx;

    std::atomic<bool>     _close_pcb { false };  // close() requested; done on the async_tcp thread
    std::atomic<bool>     _pcb_busy { false };   // bytes sent, ack not yet delivered
    std::atomic<uint32_t> _pcb_sent_at { 0 };
    uint32_t _rx_last_packet = 0;
    size_t   _rx_ack_len     = 0;
    bool     _peer_closed    = false;  // JS closed it; don't echo a close back

    void _close();
    bool _service(bool poll_due);  // false if the client was destroyed

    AcConnectHandler _connect_cb     = nullptr;
    void*            _connect_cb_arg = nullptr;
    AcConnectHandler _discard_cb     = nullptr;
    void*            _discard_cb_arg = nullptr;
    AcAckHandler     _sent_cb        = nullptr;
    void*            _sent_cb_arg    = nullptr;
    AcErrorHandler   _error_cb       = nullptr;
    void*            _error_cb_arg   = nullptr;
    AcDataHandler    _recv_cb        = nullptr;
    void*            _recv_cb_arg    = nullptr;
    AcTimeoutHandler _timeout_cb     = nullptr;
    void*            _timeout_cb_arg = nullptr;
    AcPollHandler    _poll_cb        = nullptr;
    void*            _poll_cb_arg    = nullptr;

    std::atomic<tcp_state> _tcp_state { CLOSED };
    uint32_t  _rx_timeout   = 0;
    uint32_t  _ack_timeout  = ASYNC_MAX_ACK_TIME;
    bool      _no_delay     = false;
    uint32_t  _remote_addr  = 0;
    uint16_t  _remote_port  = 0;
    uint32_t  _local_addr   = 0;
    uint16_t  _local_port   = 0;

public:
    AsyncClient* prev = nullptr;
    AsyncClient* next = nullptr;

    AsyncClient();
    ~AsyncClient();

    bool connect(IPAddress ip, uint16_t port);
    bool connect(const char* host, uint16_t port);
    void close(bool now = false);
    void stop();
    void abort();
    bool free();

    size_t add(const char* data, size_t size, uint8_t apiflags = 0);
    size_t write(const char* data);
    size_t write(const char* data, size_t size, uint8_t apiflags = 0);
    bool   send();

    size_t ack(size_t len);
    void   ackLater();

    uint8_t state() const;
    bool    connecting() const;
    bool    connected() const;
    bool    disconnecting() const;
    bool    disconnected() const;
    bool    freeable() const;
    bool    canSend() const;

    void     setRxTimeout(uint32_t timeout);
    uint32_t getRxTimeout() const;
    void     setAckTimeout(uint32_t timeout);
    uint32_t getAckTimeout() const;
    void     setNoDelay(bool nodelay);
    bool     getNoDelay() const;

    uint16_t getMss() const { return TCP_MSS; }
    uint32_t getRemoteAddress() const { return _remote_addr; }
    uint16_t getRemotePort() const { return _remote_port; }
    uint32_t getLocalAddress() const { return _local_addr; }
    uint16_t getLocalPort() const { return _local_port; }

    IPAddress remoteIP() const;
    uint16_t  remotePort() const;
    IPAddress localIP() const;
    uint16_t  localPort() const;

    size_t space() const;

    void onConnect(AcConnectHandler cb, void* arg = nullptr);
    void onDisconnect(AcConnectHandler cb, void* arg = nullptr);
    void onAck(AcAckHandler cb, void* arg = nullptr);
    void onError(AcErrorHandler cb, void* arg = nullptr);
    void onData(AcDataHandler cb, void* arg = nullptr);
    void onTimeout(AcTimeoutHandler cb, void* arg = nullptr);
    void onPoll(AcPollHandler cb, void* arg = nullptr);

    const char* errorToString(err_t error) const;
    const char* stateToString() const;
};

class AsyncServer {
protected:
    friend class BrowserAsyncTCP;

    uint16_t         _port           = 0;
    IPAddress        _addr           = IPADDR_ANY;
    bool             _noDelay        = false;
    bool             _listening      = false;
    AcConnectHandler _connect_cb     = nullptr;
    void*            _connect_cb_arg = nullptr;

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
