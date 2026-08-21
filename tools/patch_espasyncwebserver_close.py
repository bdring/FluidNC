"""Make ESPAsyncWebServer close retryable after an allocation failure."""

from __future__ import annotations

import argparse
from pathlib import Path


ORIGINAL = r'''void AsyncWebSocketClient::close(uint16_t code, const char *message) {
  if (_status != WS_CONNECTED) {
    return;
  }

  async_ws_log_w("[%s][%" PRIu32 "] CLOSE", _server->url(), _clientId);

  _status = WS_DISCONNECTING;

  if (code) {
    uint8_t packetLen = 2;
    if (message != NULL) {
      size_t mlen = strlen(message);
      if (mlen > 123) {
        mlen = 123;
      }
      packetLen += mlen;
    }
    char *buf = (char *)malloc(packetLen);
    if (buf != NULL) {
      buf[0] = (uint8_t)(code >> 8);
      buf[1] = (uint8_t)(code & 0xFF);
      if (message != NULL) {
        memcpy(buf + 2, message, packetLen - 2);
      }
      _queueControl(WS_DISCONNECT, (uint8_t *)buf, packetLen);
      free(buf);
      return;
    } else {
      async_ws_log_e("Failed to allocate");
      // Reads _client, then dereference it without any lock.
      // A concurrent _onDisconnect could null + delete the client between the check and the use.
      // Local capture ensures the pointer is read exactly once, eliminating the null-dereference.
      // (TOCTOU)
      AsyncClient *c = _client;
      if (c) {
        c->abort();
      }
      return;
    }
  }
  _queueControl(WS_DISCONNECT);
}
'''


PATCHED = r'''void AsyncWebSocketClient::close(uint16_t code, const char *message) {
  if (_status != WS_CONNECTED) {
    return;
  }

  async_ws_log_w("[%s][%" PRIu32 "] CLOSE", _server->url(), _clientId);

  _status = WS_DISCONNECTING;

  // FluidNC resource-pressure retry fix: queue construction uses throwing STL
  // containers. If it cannot enqueue the close frame, restore CONNECTED so an
  // ID-based close under AsyncWebSocket::_ws_clients_lock can retry later.
  try {
    if (code) {
      uint8_t packetLen = 2;
      if (message != NULL) {
        size_t mlen = strlen(message);
        if (mlen > 123) {
          mlen = 123;
        }
        packetLen += mlen;
      }
      char *buf = (char *)malloc(packetLen);
      if (buf != NULL) {
        buf[0] = (uint8_t)(code >> 8);
        buf[1] = (uint8_t)(code & 0xFF);
        if (message != NULL) {
          memcpy(buf + 2, message, packetLen - 2);
        }
        bool queued = false;
        try {
          queued = _queueControl(WS_DISCONNECT, (uint8_t *)buf, packetLen);
        } catch (...) {
          free(buf);
          throw;
        }
        free(buf);
        if (!queued) {
          _status = WS_CONNECTED;
        }
        return;
      } else {
        async_ws_log_e("Failed to allocate");
        AsyncClient *c = _client;
        if (c) {
          c->abort();
        } else {
          _status = WS_CONNECTED;
        }
        return;
      }
    }
    if (!_queueControl(WS_DISCONNECT)) {
      _status = WS_CONNECTED;
    }
  } catch (...) {
    _status = WS_CONNECTED;
    throw;
  }
}
'''

HANDSHAKE_REJECT_ORIGINAL = r'''  if (_handshakeHandler != nullptr) {
    if (!_handshakeHandler(request)) {
      request->send(401);
      return;
    }
  }
'''

HANDSHAKE_REJECT_PATCHED = r'''  if (_handshakeHandler != nullptr) {
    if (!_handshakeHandler(request)) {
      // FluidNC resource-pressure handshake rejection: a normal status-only
      // response still allocates headers and can silently emit zero bytes
      // after the admission guard has rejected the socket.  Abort directly so
      // the peer observes a bounded TCP failure instead of waiting for its
      // handshake timeout.  The FluidNC handler increments its admission
      // counter before returning false, so this remains observable.
      request->abort();
      return;
    }
  }
'''

SERVER_CLOSE_ORIGINAL = r'''void AsyncWebSocket::close(uint32_t id, uint16_t code, const char *message) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  if (AsyncWebSocketClient *c = client(id)) {
    c->close(code, message);
  }
}
'''

HANDSHAKE_REJECT_TIMING_MARKER = "FluidNC resource-pressure reject abort latency diagnostic"

HANDSHAKE_REJECT_TIMING_PATCHED = r'''  if (_handshakeHandler != nullptr) {
    if (!_handshakeHandler(request)) {
      // FluidNC resource-pressure handshake rejection: a normal status-only
      // response still allocates headers and can silently emit zero bytes
      // after the admission guard has rejected the socket.  Abort directly so
      // the peer observes a bounded TCP failure instead of waiting for its
      // handshake timeout.  The FluidNC handler increments its admission
      // counter before returning false, so this remains observable.
      // FluidNC resource-pressure reject abort latency diagnostic: this is
      // deliberately around only the synchronous transport abort.  A slow
      // peer result with a small value occurred before this request handler.
      async_web_resource_reject_abort_calls_counter.fetch_add(1, std::memory_order_relaxed);
      const uint32_t abort_started_us = micros();
      request->abort();
      const uint32_t abort_elapsed_us = static_cast<uint32_t>(micros() - abort_started_us);
      uint32_t observed = async_web_resource_reject_abort_max_us_counter.load(std::memory_order_relaxed);
      while (observed < abort_elapsed_us &&
             !async_web_resource_reject_abort_max_us_counter.compare_exchange_weak(
                 observed, abort_elapsed_us, std::memory_order_relaxed, std::memory_order_relaxed)) {
      }
      return;
    }
  }
'''

DIAGNOSTIC_INCLUDE_ORIGINAL = r'''#include <algorithm>
#include <cstdio>'''

DIAGNOSTIC_INCLUDE_PATCHED = r'''#include <algorithm>
#include <atomic>
#include <cstdio>'''

DIAGNOSTIC_COUNTERS_ORIGINAL = r'''using namespace asyncsrv;
'''

DIAGNOSTIC_COUNTERS_PATCHED = r'''using namespace asyncsrv;

namespace {
std::atomic<uint32_t> async_web_resource_reject_abort_calls_counter { 0 };
std::atomic<uint32_t> async_web_resource_reject_abort_max_us_counter { 0 };
}

extern "C" uint32_t async_web_resource_reject_abort_calls() {
  return async_web_resource_reject_abort_calls_counter.load(std::memory_order_relaxed);
}

extern "C" uint32_t async_web_resource_reject_abort_max_us() {
  return async_web_resource_reject_abort_max_us_counter.load(std::memory_order_relaxed);
}
'''

SERVER_CLOSE_PATCHED = SERVER_CLOSE_ORIGINAL + r'''
bool AsyncWebSocket::abort(uint32_t id) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  const auto iter = std::find_if(_clients.begin(), _clients.end(), [id](const AsyncWebSocketClient &client) {
    return client.id() == id && client.status() == WS_CONNECTED;
  });
  if (iter == _clients.end()) {
    return false;
  }

  AsyncClient *transport = iter->client();
  if (!transport) {
    return false;
  }

  // abort() synchronously runs the disconnect callback, which erases this
  // list element. Do not touch the iterator or client after this call.
  transport->abort();
  return true;
}
'''

HEADER_CLOSE_ORIGINAL = r'''  void close(uint32_t id, uint16_t code = 0, const char *message = NULL);
  void closeAll(uint16_t code = 0, const char *message = NULL);
'''

HEADER_CLOSE_PATCHED = r'''  void close(uint32_t id, uint16_t code = 0, const char *message = NULL);
  bool abort(uint32_t id);
  void closeAll(uint16_t code = 0, const char *message = NULL);
'''

QUEUE_LENGTH_ORIGINAL = r'''bool AsyncWebSocket::availableForWrite(uint32_t id) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  const auto iter = std::find_if(std::begin(_clients), std::end(_clients), [id](const AsyncWebSocketClient &c) {
    return c.id() == id;
  });
  if (iter == std::end(_clients)) {
    return true;
  }
  return !iter->queueIsFull();
}
'''

QUEUE_LENGTH_PATCHED = QUEUE_LENGTH_ORIGINAL + r'''
bool AsyncWebSocket::queueLength(uint32_t id, size_t &length) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  const auto iter = std::find_if(std::begin(_clients), std::end(_clients), [id](const AsyncWebSocketClient &c) {
    return c.id() == id && c.status() == WS_CONNECTED;
  });
  if (iter == std::end(_clients)) {
    return false;
  }
  length = iter->queueLen();
  return true;
}
'''

HEADER_QUEUE_LENGTH_ORIGINAL = r'''  bool availableForWriteAll();
  bool availableForWrite(uint32_t id);

  size_t count() const;
'''

HEADER_QUEUE_LENGTH_PATCHED = r'''  bool availableForWriteAll();
  bool availableForWrite(uint32_t id);
  bool queueLength(uint32_t id, size_t &length);

  size_t count() const;
'''


def patch_source(source: Path) -> bool:
    text = source.read_text(encoding="utf-8")
    changed = False
    if DIAGNOSTIC_INCLUDE_PATCHED not in text:
        if DIAGNOSTIC_INCLUDE_ORIGINAL not in text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer diagnostic includes: {source}")
        text = text.replace(DIAGNOSTIC_INCLUDE_ORIGINAL, DIAGNOSTIC_INCLUDE_PATCHED, 1)
        changed = True
    if DIAGNOSTIC_COUNTERS_PATCHED not in text:
        if DIAGNOSTIC_COUNTERS_ORIGINAL not in text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer diagnostic counters: {source}")
        text = text.replace(DIAGNOSTIC_COUNTERS_ORIGINAL, DIAGNOSTIC_COUNTERS_PATCHED, 1)
        changed = True
    if HANDSHAKE_REJECT_TIMING_PATCHED not in text:
        if HANDSHAKE_REJECT_PATCHED in text:
            text = text.replace(HANDSHAKE_REJECT_PATCHED, HANDSHAKE_REJECT_TIMING_PATCHED, 1)
        elif HANDSHAKE_REJECT_ORIGINAL in text:
            text = text.replace(HANDSHAKE_REJECT_ORIGINAL, HANDSHAKE_REJECT_TIMING_PATCHED, 1)
        else:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer handshake rejection implementation: {source}")
        changed = True
    if PATCHED not in text:
        if ORIGINAL not in text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer close implementation: {source}")
        text = text.replace(ORIGINAL, PATCHED, 1)
        changed = True
    if SERVER_CLOSE_PATCHED not in text:
        if SERVER_CLOSE_ORIGINAL not in text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer server-close implementation: {source}")
        text = text.replace(SERVER_CLOSE_ORIGINAL, SERVER_CLOSE_PATCHED, 1)
        changed = True
    if QUEUE_LENGTH_PATCHED not in text:
        if QUEUE_LENGTH_ORIGINAL not in text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer queue-length implementation: {source}")
        text = text.replace(QUEUE_LENGTH_ORIGINAL, QUEUE_LENGTH_PATCHED, 1)
        changed = True

    header = source.with_name("AsyncWebSocket.h")
    header_text = header.read_text(encoding="utf-8")
    header_changed = False
    if HEADER_CLOSE_PATCHED not in header_text:
        if HEADER_CLOSE_ORIGINAL not in header_text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer close declaration: {header}")
        header_text = header_text.replace(HEADER_CLOSE_ORIGINAL, HEADER_CLOSE_PATCHED, 1)
        header_changed = True
    if HEADER_QUEUE_LENGTH_PATCHED not in header_text:
        if HEADER_QUEUE_LENGTH_ORIGINAL not in header_text:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer queue-length declaration: {header}")
        header_text = header_text.replace(HEADER_QUEUE_LENGTH_ORIGINAL, HEADER_QUEUE_LENGTH_PATCHED, 1)
        header_changed = True

    if changed:
        source.write_text(text, encoding="utf-8", newline="\n")
    if header_changed:
        header.write_text(header_text, encoding="utf-8", newline="\n")
    return changed or header_changed


def platformio_patch(env) -> None:
    source = (
        Path(env.subst("$PROJECT_LIBDEPS_DIR"))
        / env.subst("$PIOENV")
        / "ESPAsyncWebServer"
        / "src"
        / "AsyncWebSocket.cpp"
    )
    patch_source(source)


if "Import" in globals():
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    platformio_patch(env)  # type: ignore[name-defined]  # noqa: F821


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    patch_source(args.source)
