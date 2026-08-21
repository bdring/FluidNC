"""Apply FluidNC's version-bound AsyncTCP lifecycle diagnostics."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


EXPECTED_VERSION = "3.5.0"
MARKER = "async_tcp_event_queue_high_water"

R10_COUNTER_BLOCK = '''std::atomic<uint32_t> async_last_rx_timeout_idle_ms { 0 };
}

extern "C" uint32_t async_tcp_client_created() { return async_client_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_client_destroyed() { return async_client_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_created() { return async_event_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_destroyed() { return async_event_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_queue_high_water() { return async_event_queue_high_water.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_rx_timeouts() { return async_rx_timeout_count.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_last_rx_timeout_idle_ms() { return async_last_rx_timeout_idle_ms.load(std::memory_order_relaxed); }'''

R11_COUNTER_BLOCK = '''std::atomic<uint32_t> async_last_rx_timeout_idle_ms { 0 };
std::atomic<uint32_t> async_accept_event_allocation_failures { 0 };
}

extern "C" uint32_t async_tcp_client_created() { return async_client_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_client_destroyed() { return async_client_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_created() { return async_event_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_destroyed() { return async_event_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_queue_high_water() { return async_event_queue_high_water.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_rx_timeouts() { return async_rx_timeout_count.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_last_rx_timeout_idle_ms() { return async_last_rx_timeout_idle_ms.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_accept_event_allocation_failures() {
  return async_accept_event_allocation_failures.load(std::memory_order_relaxed);
}'''

R12_COUNTER_BLOCK = '''std::atomic<uint32_t> async_last_rx_timeout_idle_ms { 0 };
std::atomic<uint32_t> async_accept_event_allocation_failures { 0 };
std::atomic<uint32_t> async_server_accept_admission_rejections { 0 };
std::atomic<uint32_t> async_server_pending_accepts { 0 };
std::atomic<uint32_t> async_server_accept_last_observed_free { 0 };
std::atomic<uint32_t> async_server_accept_last_largest_block { 0 };
std::atomic<uint32_t> async_server_accept_last_effective_free { 0 };
std::atomic<uint32_t> async_server_accept_last_live_clients { 0 };

void release_async_server_pending_accept() {
  uint32_t pending = async_server_pending_accepts.load(std::memory_order_relaxed);
  while (pending != 0 && !async_server_pending_accepts.compare_exchange_weak(
           pending, pending - 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}
}

extern "C" uint32_t async_tcp_client_created() { return async_client_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_client_destroyed() { return async_client_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_created() { return async_event_created.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_destroyed() { return async_event_destroyed.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_event_queue_high_water() { return async_event_queue_high_water.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_rx_timeouts() { return async_rx_timeout_count.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_last_rx_timeout_idle_ms() { return async_last_rx_timeout_idle_ms.load(std::memory_order_relaxed); }
extern "C" uint32_t async_tcp_accept_event_allocation_failures() {
  return async_accept_event_allocation_failures.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_admission_rejections() {
  return async_server_accept_admission_rejections.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_server_pending_accepts() {
  return async_server_pending_accepts.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_last_observed_free() {
  return async_server_accept_last_observed_free.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_last_largest_block() {
  return async_server_accept_last_largest_block.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_last_effective_free() {
  return async_server_accept_last_effective_free.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_last_live_clients() {
  return async_server_accept_last_live_clients.load(std::memory_order_relaxed);
}'''

R17_COUNTER_BLOCK = R12_COUNTER_BLOCK.replace(
    '''std::atomic<uint32_t> async_server_accept_last_live_clients { 0 };

void release_async_server_pending_accept()''',
    '''std::atomic<uint32_t> async_server_accept_last_live_clients { 0 };
std::atomic<uint32_t> async_accept_rst_before_dispatch_cleanups { 0 };

void release_async_server_pending_accept()''',
    1,
).replace(
    '''extern "C" uint32_t async_tcp_accept_last_live_clients() {
  return async_server_accept_last_live_clients.load(std::memory_order_relaxed);
}''',
    '''extern "C" uint32_t async_tcp_accept_last_live_clients() {
  return async_server_accept_last_live_clients.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_early_rst_accept_cleanups() {
  return async_accept_rst_before_dispatch_cleanups.load(std::memory_order_relaxed);
}''',
    1,
)

R19_UNQUALIFIED_COUNTER_BLOCK = R17_COUNTER_BLOCK.replace(
    '''std::atomic<uint32_t> async_accept_rst_before_dispatch_cleanups { 0 };

void release_async_server_pending_accept()''',
    '''std::atomic<uint32_t> async_accept_rst_before_dispatch_cleanups { 0 };
std::atomic<uint32_t> async_tcp_accept_callbacks { 0 };
std::atomic<uint32_t> async_tcp_accept_null_pcbs { 0 };
std::atomic<int32_t> async_tcp_accept_last_null_pcb_error { 0 };
std::atomic<uint32_t> async_tcp_accept_client_allocation_failures { 0 };
std::atomic<uint32_t> async_tcp_accept_client_setup_failures { 0 };

void release_async_server_pending_accept()''',
    1,
).replace(
    '''extern "C" uint32_t async_tcp_early_rst_accept_cleanups() {
  return async_accept_rst_before_dispatch_cleanups.load(std::memory_order_relaxed);
}''',
    '''extern "C" uint32_t async_tcp_early_rst_accept_cleanups() {
  return async_accept_rst_before_dispatch_cleanups.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_callbacks() {
  return async_tcp_accept_callbacks.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_null_pcbs() {
  return async_tcp_accept_null_pcbs.load(std::memory_order_relaxed);
}
extern "C" int32_t async_tcp_accept_last_null_pcb_error() {
  return async_tcp_accept_last_null_pcb_error.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_client_allocation_failures() {
  return async_tcp_accept_client_allocation_failures.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_accept_client_setup_failures() {
  return async_tcp_accept_client_setup_failures.load(std::memory_order_relaxed);
}''',
    1,
)

CURRENT_COUNTER_BLOCK = R19_UNQUALIFIED_COUNTER_BLOCK.replace(
    "std::atomic<uint32_t> async_tcp_accept_callbacks { 0 };",
    "std::atomic<uint32_t> async_accept_callbacks_count { 0 };",
    1,
).replace(
    "std::atomic<uint32_t> async_tcp_accept_null_pcbs { 0 };",
    "std::atomic<uint32_t> async_accept_null_pcb_count { 0 };",
    1,
).replace(
    "std::atomic<int32_t> async_tcp_accept_last_null_pcb_error { 0 };",
    "std::atomic<int32_t> async_accept_last_null_pcb_error_value { 0 };",
    1,
).replace(
    "std::atomic<uint32_t> async_tcp_accept_client_allocation_failures { 0 };",
    "std::atomic<uint32_t> async_accept_client_allocation_failure_count { 0 };",
    1,
).replace(
    "std::atomic<uint32_t> async_tcp_accept_client_setup_failures { 0 };",
    "std::atomic<uint32_t> async_accept_client_setup_failure_count { 0 };",
    1,
).replace(
    "return async_tcp_accept_callbacks.load",
    "return async_accept_callbacks_count.load",
    1,
).replace(
    "return async_tcp_accept_null_pcbs.load",
    "return async_accept_null_pcb_count.load",
    1,
).replace(
    "return async_tcp_accept_last_null_pcb_error.load",
    "return async_accept_last_null_pcb_error_value.load",
    1,
).replace(
    "return async_tcp_accept_client_allocation_failures.load",
    "return async_accept_client_allocation_failure_count.load",
    1,
).replace(
    "return async_tcp_accept_client_setup_failures.load",
    "return async_accept_client_setup_failure_count.load",
    1,
)

# r20 is the last published diagnostic layout.  Keep it as an exact migration
# source so a project dependency cache can be upgraded in place without
# silently accepting a partially patched AsyncTCP.cpp.
R20_COUNTER_BLOCK = CURRENT_COUNTER_BLOCK

CURRENT_COUNTER_BLOCK = R20_COUNTER_BLOCK.replace(
    '''std::atomic<uint32_t> async_accept_client_setup_failure_count { 0 };

void release_async_server_pending_accept()''',
    '''std::atomic<uint32_t> async_accept_client_setup_failure_count { 0 };
std::atomic<uint32_t> async_tcp_pcb_peak_occupancy_counter { 0 };

void release_async_server_pending_accept()''',
    1,
).replace(
    '''extern "C" uint32_t async_tcp_accept_client_setup_failures() {
  return async_accept_client_setup_failure_count.load(std::memory_order_relaxed);
}''',
    '''extern "C" uint32_t async_tcp_accept_client_setup_failures() {
  return async_accept_client_setup_failure_count.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_pcb_peak_occupancy() {
  return async_tcp_pcb_peak_occupancy_counter.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_pcb_capacity() {
  return MEMP_NUM_TCP_PCB;
}''',
    1,
)

R21_COUNTER_BLOCK = CURRENT_COUNTER_BLOCK

CURRENT_COUNTER_BLOCK = R21_COUNTER_BLOCK.replace(
    '''extern "C" uint32_t async_tcp_pcb_peak_occupancy() {
  return async_tcp_pcb_peak_occupancy_counter.load(std::memory_order_relaxed);
}
extern "C" uint32_t async_tcp_pcb_capacity() {
  return MEMP_NUM_TCP_PCB;
}''',
    '''extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak() {
  return async_tcp_pcb_peak_occupancy_counter.load(std::memory_order_relaxed);
}''',
    1,
)

R17_PCB_SNAPSHOT_BLOCK = '''typedef struct {
  struct tcpip_api_call_data call;
  uint32_t active;
  uint32_t time_wait;
  uint32_t bound;
  uint32_t listening;
} async_tcp_pcb_snapshot_t;

static err_t _async_tcp_pcb_snapshot_api(struct tcpip_api_call_data *api_call_msg) {
  async_tcp_pcb_snapshot_t *snapshot = reinterpret_cast<async_tcp_pcb_snapshot_t *>(api_call_msg);
  for (tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->active;
  }
  for (tcp_pcb *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->time_wait;
  }
  for (tcp_pcb *pcb = tcp_bound_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->bound;
  }
  for (tcp_pcb_listen *pcb = tcp_listen_pcbs.listen_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->listening;
  }
  return ERR_OK;
}

extern "C" void async_tcp_pcb_snapshot(uint32_t *active, uint32_t *time_wait, uint32_t *bound, uint32_t *listening) {
  async_tcp_pcb_snapshot_t snapshot {};
  tcpip_api_call(_async_tcp_pcb_snapshot_api, &snapshot.call);
  if (active) {
    *active = snapshot.active;
  }
  if (time_wait) {
    *time_wait = snapshot.time_wait;
  }
  if (bound) {
    *bound = snapshot.bound;
  }
  if (listening) {
    *listening = snapshot.listening;
  }
}'''

CURRENT_PCB_SNAPSHOT_BLOCK = R17_PCB_SNAPSHOT_BLOCK.replace(
    '''  uint32_t listening;
} async_tcp_pcb_snapshot_t;''',
    '''  uint32_t listening;
  uint32_t listen_backlog;
  uint32_t listen_accepts_pending;
} async_tcp_pcb_snapshot_t;''',
    1,
).replace(
    '''  for (tcp_pcb_listen *pcb = tcp_listen_pcbs.listen_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->listening;
  }''',
    '''  for (tcp_pcb_listen *pcb = tcp_listen_pcbs.listen_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->listening;
#if TCP_LISTEN_BACKLOG
    snapshot->listen_backlog += pcb->backlog;
    snapshot->listen_accepts_pending += pcb->accepts_pending;
#endif
  }''',
    1,
).replace(
    '''extern "C" void async_tcp_pcb_snapshot(uint32_t *active, uint32_t *time_wait, uint32_t *bound, uint32_t *listening) {''',
    '''extern "C" void async_tcp_pcb_snapshot(uint32_t *active, uint32_t *time_wait, uint32_t *bound, uint32_t *listening,
                                       uint32_t *listen_backlog, uint32_t *listen_accepts_pending) {''',
    1,
).replace(
    '''  if (listening) {
    *listening = snapshot.listening;
  }
}''',
    '''  if (listening) {
    *listening = snapshot.listening;
  }
  if (listen_backlog) {
    *listen_backlog = snapshot.listen_backlog;
  }
  if (listen_accepts_pending) {
    *listen_accepts_pending = snapshot.listen_accepts_pending;
  }
}''',
    1,
)

R20_PCB_SNAPSHOT_BLOCK = CURRENT_PCB_SNAPSHOT_BLOCK

CURRENT_PCB_SNAPSHOT_BLOCK = R20_PCB_SNAPSHOT_BLOCK.replace(
    '''static err_t _async_tcp_pcb_snapshot_api(struct tcpip_api_call_data *api_call_msg) {''',
    '''// Runs only on the lwIP thread.  A peak equal to MEMP_NUM_TCP_PCB means
// a later SYN can be dropped before AsyncTCP receives tcp_accept().
static void _note_async_tcp_pcb_occupancy() {
  uint32_t occupancy = 0;
  for (tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    ++occupancy;
  }
  for (tcp_pcb *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next) {
    ++occupancy;
  }
  uint32_t observed = async_tcp_pcb_peak_occupancy_counter.load(std::memory_order_relaxed);
  while (observed < occupancy &&
         !async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak(
             observed, occupancy, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

static err_t _async_tcp_pcb_snapshot_api(struct tcpip_api_call_data *api_call_msg) {''',
    1,
)

R21_PCB_SNAPSHOT_BLOCK = CURRENT_PCB_SNAPSHOT_BLOCK

CURRENT_PCB_SNAPSHOT_BLOCK = R21_PCB_SNAPSHOT_BLOCK.replace(
    '''// Runs only on the lwIP thread.  A peak equal to MEMP_NUM_TCP_PCB means
// a later SYN can be dropped before AsyncTCP receives tcp_accept().''',
    '''// Runs only on the lwIP thread. This is an accept-callback high-water
// sample of active plus TIME_WAIT PCBs, not a TCP capacity or SYN-drop counter.''',
    1,
)

R17_ACCEPT_ENTRY_BLOCK = '''int8_t AsyncTCP_detail::tcp_accept(void *arg, tcp_pcb *pcb, int8_t err) {
  if (!pcb) {
    async_tcp_log_e("_accept failed: pcb is NULL");'''

CURRENT_ACCEPT_ENTRY_BLOCK = '''int8_t AsyncTCP_detail::tcp_accept(void *arg, tcp_pcb *pcb, int8_t err) {
  async_accept_callbacks_count.fetch_add(1, std::memory_order_relaxed);
  if (!pcb) {
    async_accept_null_pcb_count.fetch_add(1, std::memory_order_relaxed);
    async_accept_last_null_pcb_error_value.store(static_cast<int32_t>(err), std::memory_order_relaxed);
    async_tcp_log_e("_accept failed: pcb is NULL");'''

R20_ACCEPT_ENTRY_BLOCK = CURRENT_ACCEPT_ENTRY_BLOCK

CURRENT_ACCEPT_ENTRY_BLOCK = R20_ACCEPT_ENTRY_BLOCK.replace(
    '''  async_accept_callbacks_count.fetch_add(1, std::memory_order_relaxed);
  if (!pcb) {''',
    '''  async_accept_callbacks_count.fetch_add(1, std::memory_order_relaxed);
  _note_async_tcp_pcb_occupancy();
  if (!pcb) {''',
    1,
)

R17_CLIENT_SETUP_FAILURE_BLOCK = '''    if (c) {
      // Couldn't complete setup'''

CURRENT_CLIENT_SETUP_FAILURE_BLOCK = '''    if (c) {
      async_accept_client_setup_failure_count.fetch_add(1, std::memory_order_relaxed);
      // Couldn't complete setup'''

R17_CLIENT_ALLOCATION_FAILURE_BLOCK = '''    }
    async_tcp_log_e("_accept failed: couldn't allocate client");'''

CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK = '''    }
    async_accept_client_allocation_failure_count.fetch_add(1, std::memory_order_relaxed);
    async_tcp_log_e("_accept failed: couldn't allocate client");'''

R19_UNQUALIFIED_ACCEPT_ENTRY_BLOCK = R20_ACCEPT_ENTRY_BLOCK.replace(
    "async_accept_callbacks_count.fetch_add",
    "async_tcp_accept_callbacks.fetch_add",
    1,
).replace(
    "async_accept_null_pcb_count.fetch_add",
    "async_tcp_accept_null_pcbs.fetch_add",
    1,
).replace(
    "async_accept_last_null_pcb_error_value.store",
    "async_tcp_accept_last_null_pcb_error.store",
    1,
)

R19_UNQUALIFIED_CLIENT_SETUP_FAILURE_BLOCK = CURRENT_CLIENT_SETUP_FAILURE_BLOCK.replace(
    "async_accept_client_setup_failure_count.fetch_add",
    "async_tcp_accept_client_setup_failures.fetch_add",
    1,
)

R19_UNQUALIFIED_CLIENT_ALLOCATION_FAILURE_BLOCK = CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK.replace(
    "async_accept_client_allocation_failure_count.fetch_add",
    "async_tcp_accept_client_allocation_failures.fetch_add",
    1,
)

R11_ACCEPT_FAILURE_BLOCK = '''      // Couldn't allocate accept event. This AsyncClient has not been
      // published to the AsyncTCP task, so no later owner can destroy it.
      // Detach every lwIP callback before aborting; tcp_abort() may otherwise
      // enqueue an error event with a pointer to the client we are about to free.
      async_accept_event_allocation_failures.fetch_add(1, std::memory_order_relaxed);
      _reset_tcp_callbacks(pcb, c);
      c->_pcb = nullptr;
      tcp_abort(pcb);
      delete c;
      async_tcp_log_e("_accept failed: couldn't accept client");
      return ERR_ABRT;'''

CURRENT_ACCEPT_FAILURE_BLOCK = '''      // Couldn't allocate accept event. This AsyncClient has not been
      // published to the AsyncTCP task, so no later owner can destroy it.
      // Detach every lwIP callback before aborting; tcp_abort() may otherwise
      // enqueue an error event with a pointer to the client we are about to free.
      async_accept_event_allocation_failures.fetch_add(1, std::memory_order_relaxed);
      release_async_server_pending_accept();
      _reset_tcp_callbacks(pcb, c);
      c->_pcb = nullptr;
      tcp_abort(pcb);
      delete c;
      async_tcp_log_e("_accept failed: couldn't accept client");
      return ERR_ABRT;'''

R11_EVENT_PACKET_BLOCK = '''  inline lwip_tcp_event_packet_t(lwip_tcp_event_t _event, AsyncClient *_client) : next(nullptr), event(_event), client(_client) {
    async_event_created.fetch_add(1, std::memory_order_relaxed);
  };

  inline ~lwip_tcp_event_packet_t() {
    async_event_destroyed.fetch_add(1, std::memory_order_relaxed);
  }
};'''

CURRENT_EVENT_PACKET_BLOCK = '''  inline lwip_tcp_event_packet_t(lwip_tcp_event_t _event, AsyncClient *_client) : next(nullptr), event(_event), client(_client) {
    async_event_created.fetch_add(1, std::memory_order_relaxed);
  };

  inline ~lwip_tcp_event_packet_t() {
    if (event == LWIP_TCP_ACCEPT) {
      release_async_server_pending_accept();
    }
    async_event_destroyed.fetch_add(1, std::memory_order_relaxed);
  }
};'''

TRANSIENT_ACCEPTED_BLOCK = '''int8_t AsyncServer::_accepted(AsyncClient *client) {
  release_async_server_pending_accept();
  if (_connect_cb) {'''

UPSTREAM_ACCEPTED_BLOCK = '''int8_t AsyncServer::_accepted(AsyncClient *client) {
  if (_connect_cb) {'''

R13_RST_CLEANUP_BLOCK = '''  if (removed_unpublished_accept) {
    delete client;
    return;
  }'''

CURRENT_RST_CLEANUP_BLOCK = '''  if (removed_unpublished_accept) {
    async_accept_rst_before_dispatch_cleanups.fetch_add(1, std::memory_order_relaxed);
    delete client;
    return;
  }'''

KNOWN_PREVIOUS_PATCH_PREFIXES = {9, 10, 11, 12}

PATCHES = (
    (
        '''#include "AsyncTCPSimpleIntrusiveList.h"

/**''',
        '''#include "AsyncTCPSimpleIntrusiveList.h"

#include <atomic>

namespace {
std::atomic<uint32_t> async_client_created { 0 };
std::atomic<uint32_t> async_client_destroyed { 0 };
std::atomic<uint32_t> async_event_created { 0 };
std::atomic<uint32_t> async_event_destroyed { 0 };
std::atomic<uint32_t> async_event_queue_high_water { 0 };
std::atomic<uint32_t> async_rx_timeout_count { 0 };
''' + CURRENT_COUNTER_BLOCK + '''

/**''',
    ),
    (
        '''  inline lwip_tcp_event_packet_t(lwip_tcp_event_t _event, AsyncClient *_client) : next(nullptr), event(_event), client(_client){};
};''',
        CURRENT_EVENT_PACKET_BLOCK,
    ),
    (
        '''static SimpleIntrusiveList<lwip_tcp_event_packet_t> _async_queue;
static TaskHandle_t _async_service_task_handle = NULL;

static uint32_t _xor_shift_state''',
        '''static SimpleIntrusiveList<lwip_tcp_event_packet_t> _async_queue;
static TaskHandle_t _async_service_task_handle = NULL;

static void _update_async_queue_high_water() {
  const uint32_t depth = static_cast<uint32_t>(_async_queue.size());
  uint32_t observed = async_event_queue_high_water.load(std::memory_order_relaxed);
  while (observed < depth &&
         !async_event_queue_high_water.compare_exchange_weak(observed, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

extern "C" uint32_t async_tcp_event_queue_depth() {
  if (_async_queue_mutex == nullptr) {
    return 0;
  }
  queue_mutex_guard guard;
  return static_cast<uint32_t>(_async_queue.size());
}

static uint32_t _xor_shift_state''',
    ),
    (
        '''  _async_queue.push_back(e);
  xTaskNotifyGive(_async_service_task_handle);''',
        '''  _async_queue.push_back(e);
  _update_async_queue_high_water();
  xTaskNotifyGive(_async_service_task_handle);''',
    ),
    (
        '''  _async_queue.push_front(e);
  xTaskNotifyGive(_async_service_task_handle);''',
        '''  _async_queue.push_front(e);
  _update_async_queue_high_water();
  xTaskNotifyGive(_async_service_task_handle);''',
    ),
    (
        '''#include "lwip/priv/tcpip_priv.h"

typedef struct {''',
        '''#include "lwip/priv/tcpip_priv.h"
#include "lwip/priv/tcp_priv.h"

typedef struct {
  struct tcpip_api_call_data call;
  uint32_t active;
  uint32_t time_wait;
  uint32_t bound;
  uint32_t listening;
} async_tcp_pcb_snapshot_t;

static err_t _async_tcp_pcb_snapshot_api(struct tcpip_api_call_data *api_call_msg) {
  async_tcp_pcb_snapshot_t *snapshot = reinterpret_cast<async_tcp_pcb_snapshot_t *>(api_call_msg);
  for (tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->active;
  }
  for (tcp_pcb *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->time_wait;
  }
  for (tcp_pcb *pcb = tcp_bound_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->bound;
  }
  for (tcp_pcb_listen *pcb = tcp_listen_pcbs.listen_pcbs; pcb; pcb = pcb->next) {
    ++snapshot->listening;
  }
  return ERR_OK;
}

extern "C" void async_tcp_pcb_snapshot(uint32_t *active, uint32_t *time_wait, uint32_t *bound, uint32_t *listening) {
  async_tcp_pcb_snapshot_t snapshot {};
  tcpip_api_call(_async_tcp_pcb_snapshot_api, &snapshot.call);
  if (active) {
    *active = snapshot.active;
  }
  if (time_wait) {
    *time_wait = snapshot.time_wait;
  }
  if (bound) {
    *bound = snapshot.bound;
  }
  if (listening) {
    *listening = snapshot.listening;
  }
}

typedef struct {''',
    ),
    (
        '''  _pcb = pcb;
  if (_pcb) {''',
        '''  _pcb = pcb;
  async_client_created.fetch_add(1, std::memory_order_relaxed);
  if (_pcb) {''',
    ),
    (
        '''AsyncClient::~AsyncClient() {
  if (_pcb) {''',
        '''AsyncClient::~AsyncClient() {
  async_client_destroyed.fetch_add(1, std::memory_order_relaxed);
  if (_pcb) {''',
    ),
    (
        '''  if (_rx_timeout && (now - _rx_last_packet) >= (_rx_timeout * 1000)) {
    async_tcp_log_d("rx timeout %d", pcb->state);''',
        '''  if (_rx_timeout && (now - _rx_last_packet) >= (_rx_timeout * 1000)) {
    async_rx_timeout_count.fetch_add(1, std::memory_order_relaxed);
    async_last_rx_timeout_idle_ms.store(now - _rx_last_packet, std::memory_order_relaxed);
    async_tcp_log_d("rx timeout %d", pcb->state);''',
    ),
    (
        '''      // Couldn't allocate accept event
      // We can't let the client object call in to close, as we're on the LWIP thread; it could deadlock trying to RPC to itself
      c->_pcb = nullptr;
      tcp_abort(pcb);
      async_tcp_log_e("_accept failed: couldn't accept client");
      return ERR_ABRT;''',
        CURRENT_ACCEPT_FAILURE_BLOCK,
    ),
    (
        '''  auto server = reinterpret_cast<AsyncServer *>(arg);
  if (server->_connect_cb) {
    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
    if (c && c->pcb()) {
      c->setNoDelay(server->_noDelay);''',
        '''  auto server = reinterpret_cast<AsyncServer *>(arg);
  if (server->_connect_cb) {
    constexpr uint32_t ASYNC_SERVER_MAX_CLIENTS = 8;
    constexpr uint32_t ASYNC_SERVER_PENDING_RESERVATION = 7 * 1024;
    constexpr uint32_t ASYNC_SERVER_FIRST_CLIENT_FLOOR = 24 * 1024;
    constexpr uint32_t ASYNC_SERVER_ADDITIONAL_CLIENT_FLOOR = 32 * 1024;
    constexpr uint32_t ASYNC_SERVER_LARGEST_BLOCK_FLOOR = 20 * 1024;

    const uint32_t pending = async_server_pending_accepts.load(std::memory_order_relaxed);
    const uint32_t created = async_client_created.load(std::memory_order_relaxed);
    const uint32_t destroyed = async_client_destroyed.load(std::memory_order_relaxed);
    const uint32_t live = created - destroyed;
#if defined(ARDUINO) && !defined(LIBRETINY)
    const uint32_t observedFree = ESP.getFreeHeap();
    const uint32_t largestBlock = ESP.getMaxAllocHeap();
#else
    const uint32_t observedFree = 0xFFFFFFFFu;
    const uint32_t largestBlock = 0xFFFFFFFFu;
#endif
    const uint32_t pendingReservation = pending >= ASYNC_SERVER_MAX_CLIENTS
                                          ? observedFree
                                          : pending * ASYNC_SERVER_PENDING_RESERVATION;
    const uint32_t effectiveFree = observedFree > pendingReservation ? observedFree - pendingReservation : 0;
    const uint32_t requiredFree = live == 0 ? ASYNC_SERVER_FIRST_CLIENT_FLOOR : ASYNC_SERVER_ADDITIONAL_CLIENT_FLOOR;

    async_server_accept_last_observed_free.store(observedFree, std::memory_order_relaxed);
    async_server_accept_last_largest_block.store(largestBlock, std::memory_order_relaxed);
    async_server_accept_last_effective_free.store(effectiveFree, std::memory_order_relaxed);
    async_server_accept_last_live_clients.store(live, std::memory_order_relaxed);

    if (live >= ASYNC_SERVER_MAX_CLIENTS || effectiveFree < requiredFree || largestBlock < ASYNC_SERVER_LARGEST_BLOCK_FLOOR) {
      async_server_accept_admission_rejections.fetch_add(1, std::memory_order_relaxed);
      tcp_abort(pcb);
      return ERR_ABRT;
    }

    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
    if (c && c->pcb()) {
      async_server_pending_accepts.fetch_add(1, std::memory_order_relaxed);
      c->setNoDelay(server->_noDelay);''',
    ),
    (
        '''  } else {
    async_tcp_log_e("_accept failed: no onConnect callback");
  }
  tcp_abort(pcb);
  return ERR_OK;
}''',
        '''  } else {
    async_tcp_log_e("_accept failed: no onConnect callback");
  }
  tcp_abort(pcb);
  return ERR_ABRT;
}''',
    ),
    (
        '''static size_t _remove_events_for_client(AsyncClient *client) {
  lwip_tcp_event_packet_t *removed_event_chain;
  {
    queue_mutex_guard guard;
    removed_event_chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &pkt) {
      return pkt.client == client;
    });
  }

  size_t count = 0;
  while (removed_event_chain) {
    ++count;
    auto t = removed_event_chain;
    removed_event_chain = t->next;
    _free_event(t);
  }
  return count;
};''',
        '''static size_t _remove_events_for_client(AsyncClient *client, bool *removed_accept = nullptr) {
  lwip_tcp_event_packet_t *removed_event_chain;
  {
    queue_mutex_guard guard;
    removed_event_chain = _async_queue.remove_if([=](lwip_tcp_event_packet_t &pkt) {
      return pkt.client == client;
    });
  }

  size_t count = 0;
  while (removed_event_chain) {
    ++count;
    auto t = removed_event_chain;
    removed_event_chain = t->next;
    if (removed_accept && t->event == LWIP_TCP_ACCEPT) {
      *removed_accept = true;
    }
    _free_event(t);
  }
  return count;
};''',
    ),
    (
        '''void AsyncTCP_detail::tcp_error(void *arg, int8_t err) {
  // ets_printf("+E: 0x%08x\\n", arg);
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  if (client && client->_pcb) {
    // The pcb has already been freed by LwIP; do not attempt to clear the callbacks!
    _remove_events_for_client(client);
    client->_pcb = nullptr;
  }

  // enqueue event to be processed in the async task for the user callback
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_ERROR, client};''',
        '''void AsyncTCP_detail::tcp_error(void *arg, int8_t err) {
  // ets_printf("+E: 0x%08x\\n", arg);
  AsyncClient *client = reinterpret_cast<AsyncClient *>(arg);
  bool removed_unpublished_accept = false;
  if (client && client->_pcb) {
    // The pcb has already been freed by LwIP; do not attempt to clear the callbacks!
    _remove_events_for_client(client, &removed_unpublished_accept);
    client->_pcb = nullptr;
  }

  // An ACCEPT event owns the client until the AsyncTCP task publishes it to
  // AsyncServer. If an early RST purged that event, no callback owner exists:
  // destroy the client here and never enqueue an ERROR event with its pointer.
''' + CURRENT_RST_CLEANUP_BLOCK + '''

  // enqueue event to be processed in the async task for the user callback
  lwip_tcp_event_packet_t *e = new (std::nothrow) lwip_tcp_event_packet_t{LWIP_TCP_ERROR, client};''',
    ),
)


def upgrade_r17_diagnostics(text: str) -> tuple[str, bool]:
    """Upgrade only complete, already-applied r17 diagnostics fragments."""
    upgraded = False
    for original, patched in (
        (R17_PCB_SNAPSHOT_BLOCK, CURRENT_PCB_SNAPSHOT_BLOCK),
        (R17_ACCEPT_ENTRY_BLOCK, CURRENT_ACCEPT_ENTRY_BLOCK),
        (R17_CLIENT_SETUP_FAILURE_BLOCK, CURRENT_CLIENT_SETUP_FAILURE_BLOCK),
        (R17_CLIENT_ALLOCATION_FAILURE_BLOCK, CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK),
    ):
        original_count = text.count(original)
        patched_count = text.count(patched)
        if original_count == 1 and patched_count == 0:
            text = text.replace(original, patched, 1)
            upgraded = True
        elif original_count == 0 and patched_count == 1:
            continue
        else:
            raise RuntimeError("Unsupported AsyncTCP r17 diagnostics upgrade")
    return text, upgraded


def upgrade_unqualified_r19_diagnostics(text: str) -> tuple[str, bool]:
    """Repair only the exact pre-build r19 diagnostic naming mistake."""
    upgraded = False
    for original, patched in (
        (R19_UNQUALIFIED_ACCEPT_ENTRY_BLOCK, CURRENT_ACCEPT_ENTRY_BLOCK),
        (R19_UNQUALIFIED_CLIENT_SETUP_FAILURE_BLOCK, CURRENT_CLIENT_SETUP_FAILURE_BLOCK),
        (R19_UNQUALIFIED_CLIENT_ALLOCATION_FAILURE_BLOCK, CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK),
    ):
        original_count = text.count(original)
        patched_count = text.count(patched)
        if original_count == 1 and patched_count == 0:
            text = text.replace(original, patched, 1)
            upgraded = True
        elif original_count == 0 and patched_count in (0, 1):
            continue
        else:
            raise RuntimeError("Unsupported AsyncTCP r19 diagnostic naming upgrade")
    return text, upgraded


def upgrade_r20_diagnostics(text: str) -> tuple[str, bool]:
    """Upgrade every complete r20/r21 fragment or fail closed."""
    upgraded = False
    for originals, patched in (
        ((R20_COUNTER_BLOCK, R21_COUNTER_BLOCK), CURRENT_COUNTER_BLOCK),
        ((R20_PCB_SNAPSHOT_BLOCK, R21_PCB_SNAPSHOT_BLOCK), CURRENT_PCB_SNAPSHOT_BLOCK),
        ((R20_ACCEPT_ENTRY_BLOCK,), CURRENT_ACCEPT_ENTRY_BLOCK),
    ):
        original_count = sum(text.count(original) for original in originals)
        patched_count = text.count(patched)
        if original_count == 1 and patched_count == 0:
            original = next(original for original in originals if text.count(original) == 1)
            text = text.replace(original, patched, 1)
            upgraded = True
        elif original_count == 0 and patched_count == 1:
            continue
        else:
            raise RuntimeError("Unsupported AsyncTCP r20/r21 diagnostics upgrade")
    return text, upgraded


def patch_source(source: Path) -> bool:
    text = source.read_text(encoding="utf-8")
    migrated = False
    if CURRENT_COUNTER_BLOCK not in text:
        if R20_COUNTER_BLOCK in text:
            text = text.replace(R20_COUNTER_BLOCK, CURRENT_COUNTER_BLOCK, 1)
            migrated = True
        elif R19_UNQUALIFIED_COUNTER_BLOCK in text:
            text = text.replace(R19_UNQUALIFIED_COUNTER_BLOCK, CURRENT_COUNTER_BLOCK, 1)
            migrated = True
        else:
            previous_counter_blocks = [
                block for block in (R10_COUNTER_BLOCK, R11_COUNTER_BLOCK, R12_COUNTER_BLOCK, R17_COUNTER_BLOCK) if block in text
            ]
            if len(previous_counter_blocks) == 1:
                text = text.replace(previous_counter_blocks[0], CURRENT_COUNTER_BLOCK, 1)
                migrated = True
    text, repaired_unqualified_r19 = upgrade_unqualified_r19_diagnostics(text)
    migrated = migrated or repaired_unqualified_r19
    if R11_ACCEPT_FAILURE_BLOCK in text and CURRENT_ACCEPT_FAILURE_BLOCK not in text:
        text = text.replace(R11_ACCEPT_FAILURE_BLOCK, CURRENT_ACCEPT_FAILURE_BLOCK, 1)
        migrated = True
    if R11_EVENT_PACKET_BLOCK in text and CURRENT_EVENT_PACKET_BLOCK not in text:
        text = text.replace(R11_EVENT_PACKET_BLOCK, CURRENT_EVENT_PACKET_BLOCK, 1)
        migrated = True
    if TRANSIENT_ACCEPTED_BLOCK in text:
        text = text.replace(TRANSIENT_ACCEPTED_BLOCK, UPSTREAM_ACCEPTED_BLOCK, 1)
        migrated = True
    if R13_RST_CLEANUP_BLOCK in text and CURRENT_RST_CLEANUP_BLOCK not in text:
        text = text.replace(R13_RST_CLEANUP_BLOCK, CURRENT_RST_CLEANUP_BLOCK, 1)
        migrated = True
    # A fresh upstream source still has the pre-r17 snapshot/accept fragments.
    # Only run the r20/r21-to-current upgrader once the source actually carries
    # one of those layouts or an already complete current layout. Otherwise the
    # generic patch-state machine below owns the initial migration.
    r20_layout_present = (
        R20_PCB_SNAPSHOT_BLOCK in text
        or R21_PCB_SNAPSHOT_BLOCK in text
        or (CURRENT_PCB_SNAPSHOT_BLOCK in text and CURRENT_ACCEPT_ENTRY_BLOCK in text)
    )
    if r20_layout_present:
        text, upgraded_r20 = upgrade_r20_diagnostics(text)
        migrated = migrated or upgraded_r20
    states: list[str] = []
    for index, (original, patched) in enumerate(PATCHES):
        original_count = text.count(original)
        patched_count = text.count(patched)
        if original_count == 1 and patched_count == 0:
            states.append("original")
        elif original_count == 0 and patched_count == 1:
            states.append("patched")
        elif index == 5 and original_count == 0 and patched_count == 0 and text.count(CURRENT_PCB_SNAPSHOT_BLOCK) == 1:
            states.append("patched")
        else:
            raise RuntimeError(f"Unsupported AsyncTCP 3.5.0 implementation: {source}")

    if all(state == "patched" for state in states):
        text, upgraded = upgrade_r17_diagnostics(text)
        if migrated or upgraded:
            source.write_text(text, encoding="utf-8", newline="\n")
        return migrated or upgraded
    fresh_upstream = all(state == "original" for state in states)
    first_original = next((index for index, state in enumerate(states) if state == "original"), len(states))
    known_current_extension = CURRENT_COUNTER_BLOCK in text and first_original in {11, 12}
    previous_patch_upgrade = (
        (migrated or known_current_extension)
        and first_original in KNOWN_PREVIOUS_PATCH_PREFIXES
        and all(state == "patched" for state in states[:first_original])
        and all(state == "original" for state in states[first_original:])
    )
    if not fresh_upstream and not previous_patch_upgrade:
        raise RuntimeError(f"Partially patched AsyncTCP source: {source}")

    for (original, patched), state in zip(PATCHES, states):
        if state == "original":
            text = text.replace(original, patched, 1)
    text, _ = upgrade_r17_diagnostics(text)
    if MARKER not in text:
        raise RuntimeError(f"AsyncTCP diagnostics marker missing after patch: {source}")
    source.write_text(text, encoding="utf-8", newline="\n")
    return True


def platformio_patch(env) -> None:
    dependency = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "AsyncTCP"
    metadata = json.loads((dependency / ".piopm").read_text(encoding="utf-8"))
    if metadata.get("version") != EXPECTED_VERSION:
        raise RuntimeError(
            f"AsyncTCP version mismatch: expected {EXPECTED_VERSION}, got {metadata.get('version')}"
        )
    patch_source(dependency / "src" / "AsyncTCP.cpp")


if "Import" in globals():
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    platformio_patch(env)  # type: ignore[name-defined]  # noqa: F821


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    patch_source(args.source)
