#include "WebUI/WebResourcePolicy.h"

#include <cstddef>

using namespace WebUI::ResourcePolicy;

static_assert(first_socket_min_free_bytes == 28u * 1024u);
static_assert(first_socket_recovery_min_free_bytes == 24u * 1024u);
static_assert(additional_socket_min_free_bytes == 32u * 1024u);
static_assert(first_socket_recovery_quiet_ms == 60u * 1000u);
// After eight half-open HTTP sockets and a 125 s poll-free drain, live r8
// measured 36,649 B before the first WS candidate. Its conservative 6 KiB
// reservation left 30,505 B effective. The recovery-only floor is aligned
// with the 16 KiB emergency + 12 KiB single-file-stream reserve; every
// additional client still requires 32 KiB.
static_assert(first_socket_min_free_bytes == file_stream_emergency_reserve + file_stream_reservation_bytes);
static_assert(websocket_admission(30505u, 24u * 1024u, 0).allowed);
static_assert(!websocket_admission(28u * 1024u - 1u, 64u * 1024u, 0).allowed);
static_assert(websocket_admission(64u * 1024u, 64u * 1024u, 0).allowed);
// Live diagnostic evidence: the handshake callback had 42.79 KiB effective
// free and a 45.99 KiB largest block before allocating the first WS client.
static_assert(websocket_admission(42u * 1024u, 45u * 1024u, 0).allowed);
// After the hard countertest the callback still had 36.75 KiB effective free,
// but fragmentation left a 23.99 KiB largest block. Reconnect must remain possible.
static_assert(websocket_admission(36u * 1024u, 23u * 1024u, 0).allowed);
static_assert(!websocket_admission(first_socket_min_free_bytes - 1u, 64u * 1024u, 0).allowed);
static_assert(!websocket_admission(64u * 1024u, first_socket_min_largest_block - 1u, 0).allowed);
static_assert(websocket_admission(first_socket_min_free_bytes, first_socket_min_largest_block, 0).allowed);
static_assert(!websocket_admission(first_socket_recovery_min_free_bytes, first_socket_min_largest_block, 0).allowed);
static_assert(websocket_admission(first_socket_recovery_min_free_bytes, first_socket_min_largest_block, 0, true).allowed);
static_assert(websocket_admission(first_socket_recovery_min_free_bytes, first_socket_min_largest_block, 0, true).usedRecovery);
static_assert(!websocket_admission(first_socket_recovery_min_free_bytes - 1u, first_socket_min_largest_block, 0, true).allowed);
static_assert(!websocket_admission(first_socket_recovery_min_free_bytes, first_socket_min_largest_block - 1u, 0, true).allowed);

static_assert(websocket_admission(40u * 1024u, 20u * 1024u, 1).allowed);
static_assert(!websocket_admission(additional_socket_min_free_bytes - 1u, 20u * 1024u, 1).allowed);
static_assert(!websocket_admission(40u * 1024u, additional_socket_min_largest_block - 1u, 1).allowed);
static_assert(websocket_admission(additional_socket_min_free_bytes, additional_socket_min_largest_block, 1).allowed);
static_assert(max_websocket_clients == 8);
static_assert(first_socket_reservation_bytes == 6u * 1024u);
static_assert(additional_socket_reservation_bytes == 7u * 1024u);
static_assert(replacement_socket_reservation_bytes == 6u * 1024u);
static_assert(pending_socket_reservation(0, false) == first_socket_reservation_bytes);
static_assert(pending_socket_reservation(1, false) == additional_socket_reservation_bytes);
static_assert(pending_socket_reservation(7, false) == additional_socket_reservation_bytes);
static_assert(pending_socket_reservation(0, true) == replacement_socket_reservation_bytes);
static_assert(pending_socket_reservation(1, true) == replacement_socket_reservation_bytes);
static_assert(websocket_admission(128u * 1024u, 128u * 1024u, 7).allowed);
static_assert(!websocket_admission(128u * 1024u, 128u * 1024u, max_websocket_clients).allowed);
static_assert(!websocket_admission(128u * 1024u, 128u * 1024u, max_websocket_clients, true).allowed);

static_assert(max_active_file_streams == 1);
static_assert(heavy_http_reservation_bytes == 12u * 1024u);
static_assert(first_file_stream_min_free_bytes == 44u * 1024u);
static_assert(file_stream_admission(first_file_stream_min_free_bytes, file_stream_min_largest_block, 0, 0));
static_assert(!file_stream_admission(first_file_stream_min_free_bytes - 1u, file_stream_min_largest_block, 0, 0));
static_assert(!file_stream_admission(128u * 1024u, 128u * 1024u, 0, 1));
static_assert(!file_stream_admission(128u * 1024u, 128u * 1024u, max_active_file_streams));
static_assert(!file_stream_admission(128u * 1024u, file_stream_min_largest_block - 1u, 0));

static_assert(is_heavy_http_command("[ESP420]json=yes"));
static_assert(is_heavy_http_command("[esp420]JSON=YES"));
static_assert(is_heavy_http_command("$ESP420"));
static_assert(is_heavy_http_command("$esp420=yes"));
static_assert(!is_heavy_http_command("$State"));
static_assert(!is_heavy_http_command("[ESP4200]"));
static_assert(!is_heavy_http_command("$ESP4200"));
static_assert(!is_heavy_http_command(nullptr));

static_assert(heavy_http_admission(heavy_http_min_free_bytes, heavy_http_min_largest_block, 0, 0));
static_assert(!heavy_http_admission(heavy_http_min_free_bytes - 1u, heavy_http_min_largest_block, 0, 0));
static_assert(!heavy_http_admission(128u * 1024u, heavy_http_min_largest_block - 1u, 0, 0));
static_assert(!heavy_http_admission(128u * 1024u, 128u * 1024u, 1, 0));
static_assert(!heavy_http_admission(128u * 1024u, 128u * 1024u, 0, 1));

static_assert(!websocket_pong_expired(websocket_pong_timeout_ms, 0));
static_assert(websocket_pong_expired(websocket_pong_timeout_ms + 1u, 0));
static_assert(!websocket_pong_expired(100u, 101u));
static_assert(!websocket_pong_expired(10u, UINT32_MAX - 9u));
static_assert(websocket_pong_expired(10u, UINT32_MAX - websocket_pong_timeout_ms));
static_assert(!first_socket_recovery_timer_mature(60001u, 1u, false));
static_assert(first_socket_recovery_timer_mature(60002u, 1u, false));
static_assert(first_socket_recovery_timer_mature(0x80000010u, 1u, false));
static_assert(first_socket_recovery_timer_mature(10u, UINT32_MAX - first_socket_recovery_quiet_ms, false));
static_assert(first_socket_recovery_timer_mature(10u, 1u, true));
static_assert(!first_socket_recovery_timer_mature(60002u, 0u, false));
static_assert(!first_socket_recovery_timer_mature(0u, UINT32_MAX, false));
static_assert(first_socket_recovery_eligible(true, 0));
static_assert(!first_socket_recovery_eligible(false, 0));
static_assert(!first_socket_recovery_eligible(true, 1));
static_assert(first_socket_zero_idle_ms(60002u, 1u, false) == 60001u);
static_assert(first_socket_zero_idle_ms(100u, 101u, false) == 0u);
static_assert(first_socket_zero_idle_ms(0u, UINT32_MAX, false) == 1u);
static_assert(first_socket_zero_idle_ms(10u, 1u, true) >= first_socket_recovery_quiet_ms + 1u);
