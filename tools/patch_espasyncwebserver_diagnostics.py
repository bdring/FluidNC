"""Apply request-lifecycle diagnostics to the reviewed ESPAsyncWebServer commit."""

from __future__ import annotations

import argparse
from pathlib import Path


EXPECTED_COMMIT = "d009eff9ee94f92beccdf5812d89ec79aa44a6c1"
HEADER_MARKER = "class AsyncRequestOwnerAllocator"
REQUEST_MARKER = "async_web_request_owner_allocations"
RESPONSE_MARKER = "FluidNC zero-byte response retry credit fix"

HEADER_PATCHES = (
    (
        '''#include <memory>
#include <tuple>''',
        '''#include <memory>
#include <limits>
#include <new>
#include <tuple>''',
    ),
    (
        '''#include <vector>

#define __asyncws_unused __attribute__((unused))''',
        '''#include <vector>

extern "C" void async_web_request_owner_allocated();
extern "C" void async_web_request_owner_deallocated();

template <typename T> class AsyncRequestOwnerAllocator {
public:
  using value_type = T;

  AsyncRequestOwnerAllocator() noexcept = default;
  template <typename U> AsyncRequestOwnerAllocator(const AsyncRequestOwnerAllocator<U> &) noexcept {}

  T *allocate(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    T *memory = static_cast<T *>(::operator new(count * sizeof(T)));
    async_web_request_owner_allocated();
    return memory;
  }

  void deallocate(T *memory, std::size_t) noexcept {
    async_web_request_owner_deallocated();
    ::operator delete(memory);
  }

  template <typename U, typename... Args> void construct(U *memory, Args &&...args) {
    ::new (static_cast<void *>(memory)) U(std::forward<Args>(args)...);
  }

  template <typename U> bool operator==(const AsyncRequestOwnerAllocator<U> &) const noexcept { return true; }
  template <typename U> bool operator!=(const AsyncRequestOwnerAllocator<U> &) const noexcept { return false; }
};

#define __asyncws_unused __attribute__((unused))''',
    ),
    (
        '''  static bool _getEtag(File gzFile, char *eTag);

  // Constructor is private to ensure factory is used to create shared_ptrs
  AsyncWebServerRequest(AsyncWebServer *, AsyncClient *);''',
        '''  static bool _getEtag(File gzFile, char *eTag);

  template <typename> friend class AsyncRequestOwnerAllocator;

  // Constructor is private to ensure factory is used to create shared_ptrs
  AsyncWebServerRequest(AsyncWebServer *, AsyncClient *);''',
    ),
    (
        '''  static std::shared_ptr<AsyncWebServerRequest> create(AsyncWebServer *server, AsyncClient *client) {
    AsyncWebServerRequest *req = new (std::nothrow) AsyncWebServerRequest(server, client);
    if (req) {
      req->_this = std::shared_ptr<AsyncWebServerRequest>(req);  // store shared pointer to this request
      return req->_this;
    }
    return {};  // empty shared_ptr
  }''',
        '''  static std::shared_ptr<AsyncWebServerRequest> create(AsyncWebServer *server, AsyncClient *client) {
    try {
      auto req = std::allocate_shared<AsyncWebServerRequest>(AsyncRequestOwnerAllocator<AsyncWebServerRequest> {}, server, client);
      req->_this = req;  // store shared pointer to this request
      return req;
    } catch (const std::bad_alloc &) {
      return {};  // caller still owns client when allocation fails
    }
  }''',
    ),
)

REQUEST_PATCHES = (
    (
        '''#include <algorithm>
#include <cstring>''',
        '''#include <algorithm>
#include <atomic>
#include <cstring>''',
    ),
    (
        '''#include "./literals.h"

static inline bool isParamChar''',
        '''#include "./literals.h"

namespace {
std::atomic<uint32_t> request_created { 0 };
std::atomic<uint32_t> request_destroyed { 0 };
std::atomic<uint32_t> request_owner_allocated { 0 };
std::atomic<uint32_t> request_owner_deallocated { 0 };
}

extern "C" void async_web_request_owner_allocated() { request_owner_allocated.fetch_add(1, std::memory_order_relaxed); }
extern "C" void async_web_request_owner_deallocated() { request_owner_deallocated.fetch_add(1, std::memory_order_relaxed); }
extern "C" uint32_t async_web_request_created() { return request_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_web_request_destroyed() { return request_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_web_request_owner_allocations() { return request_owner_allocated.load(std::memory_order_relaxed); }
extern "C" uint32_t async_web_request_owner_deallocations() { return request_owner_deallocated.load(std::memory_order_relaxed); }

static inline bool isParamChar''',
    ),
    (
        '''    _tempObject(NULL) {
  c->onError(''',
        '''    _tempObject(NULL) {
  request_created.fetch_add(1, std::memory_order_relaxed);
  c->onError(''',
    ),
    (
        '''AsyncWebServerRequest::~AsyncWebServerRequest() {
  if (_client) {''',
        '''AsyncWebServerRequest::~AsyncWebServerRequest() {
  request_destroyed.fetch_add(1, std::memory_order_relaxed);
  if (_client) {''',
    ),
)


RESPONSE_PATCHES = (
    (
        '''    // execute sending whatever we have in sock buffs now
    request->client()->send();
    _writtenLength += payloadlen;
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
    _in_flight += payloadlen;
    --_in_flight_credit;  // take a credit
#endif
    if (_send_buffer_len == 0) {''',
        '''    // execute sending whatever we have in sock buffs now
    request->client()->send();
    _writtenLength += payloadlen;
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
    // FluidNC zero-byte response retry credit fix: RESPONSE_TRY_AGAIN did
    // not queue bytes, so consuming a credit here would permanently suppress
    // every later poll/ACK that is needed once the producer has more data.
    if (payloadlen) {
      _in_flight += payloadlen;
      --_in_flight_credit;  // take a credit
    }
#endif
    if (_send_buffer_len == 0) {''',
    ),
)


def _patch_file(source: Path, patches: tuple[tuple[str, str], ...], label: str) -> bool:
    text = source.read_text(encoding="utf-8")
    states: list[str] = []
    for original, patched in patches:
        original_count = text.count(original)
        patched_count = text.count(patched)
        if original_count == 1 and patched_count == 0:
            states.append("original")
        elif original_count == 0 and patched_count == 1:
            states.append("patched")
        else:
            raise RuntimeError(f"Unsupported ESPAsyncWebServer {label} implementation: {source}")
    if all(state == "patched" for state in states):
        return False
    if not all(state == "original" for state in states):
        raise RuntimeError(f"Partially patched ESPAsyncWebServer {label}: {source}")
    for original, patched in patches:
        text = text.replace(original, patched, 1)
    source.write_text(text, encoding="utf-8", newline="\n")
    return True


def patch_sources(header: Path, request: Path, response: Path) -> bool:
    header_changed = _patch_file(header, HEADER_PATCHES, "header")
    request_changed = _patch_file(request, REQUEST_PATCHES, "request")
    response_changed = _patch_file(response, RESPONSE_PATCHES, "response")
    return header_changed or request_changed or response_changed


def platformio_patch(env) -> None:
    dependency = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "ESPAsyncWebServer"
    patch_sources(
        dependency / "src" / "ESPAsyncWebServer.h",
        dependency / "src" / "WebRequest.cpp",
        dependency / "src" / "WebResponses.cpp",
    )


if "Import" in globals():
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    platformio_patch(env)  # type: ignore[name-defined]  # noqa: F821


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("request", type=Path)
    parser.add_argument("response", type=Path)
    args = parser.parse_args()
    patch_sources(args.header, args.request, args.response)
