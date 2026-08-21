#pragma once

#include <cstddef>
#include <cstdint>

namespace WebUI::ResourcePolicy {
    constexpr std::size_t max_websocket_clients             = 8;
    constexpr std::size_t first_socket_min_free_bytes       = 28u * 1024u;
    constexpr std::size_t first_socket_recovery_min_free_bytes = 24u * 1024u;
    constexpr std::size_t additional_socket_min_free_bytes  = 32u * 1024u;
    constexpr std::size_t first_socket_min_largest_block    = 20u * 1024u;
    constexpr std::size_t additional_socket_min_largest_block = 12u * 1024u;
    constexpr std::size_t first_socket_reservation_bytes    = 6u * 1024u;
    constexpr std::size_t additional_socket_reservation_bytes = 7u * 1024u;
    constexpr std::size_t replacement_socket_reservation_bytes = 6u * 1024u;
    constexpr uint32_t    pending_socket_timeout_ms          = 5u * 1000u;
    constexpr uint32_t    websocket_pong_timeout_ms          = 35u * 1000u;
    constexpr uint32_t    first_socket_recovery_quiet_ms     = 60u * 1000u;
    constexpr std::size_t max_active_file_streams           = 1;
    constexpr std::size_t first_file_stream_min_free_bytes  = 44u * 1024u;
    constexpr std::size_t file_stream_emergency_reserve     = 16u * 1024u;
    constexpr std::size_t file_stream_reservation_bytes     = 12u * 1024u;
    constexpr std::size_t file_stream_min_largest_block     = 12u * 1024u;
    constexpr std::size_t max_active_heavy_http_responses   = 1;
    constexpr std::size_t heavy_http_reservation_bytes      = 12u * 1024u;
    constexpr std::size_t heavy_http_min_free_bytes         = 32u * 1024u;
    constexpr std::size_t heavy_http_min_largest_block      = 12u * 1024u;

    enum class WebSocketAdmissionReason : uint8_t { Allowed, ClientLimit, HeapReserve };

    struct WebSocketAdmission {
        bool                     allowed;
        WebSocketAdmissionReason reason;
        bool                     usedRecovery;
    };

    struct RuntimeSnapshot {
        std::size_t pendingWebSockets;
        std::size_t activeWebSockets;
        std::size_t connectingWebSockets;
        std::size_t deferredWebSocketCloses;
        std::size_t activeFileStreams;
        std::size_t activeHeavyHttpResponses;
        uint32_t    webSocketClientLimitRejections;
        uint32_t    webSocketHeapRejections;
        uint32_t    webSocketRecoveryAdmissions;
        uint32_t    fileStreamStarts;
        uint32_t    fileStreamCompletions;
        uint32_t    fileStreamRejections;
        uint32_t    heavyHttpRejections;
        std::size_t lastWebSocketObservedFree;
        std::size_t lastWebSocketLargestBlock;
        std::size_t lastWebSocketEffectiveFree;
        std::size_t lastWebSocketOccupiedSlots;
        uint32_t    webSocketZeroIdleMs;
    };

    RuntimeSnapshot runtime_snapshot();

    constexpr std::size_t pending_socket_reservation(std::size_t occupiedSlots, bool replacesSession) {
        if (replacesSession) {
            return replacement_socket_reservation_bytes;
        }
        return occupiedSlots == 0 ? first_socket_reservation_bytes : additional_socket_reservation_bytes;
    }

    constexpr WebSocketAdmission websocket_admission(
        std::size_t freeHeap, std::size_t largestBlock, std::size_t occupiedSlots, bool recoveryEligible = false) {
        if (occupiedSlots >= max_websocket_clients) {
            return { false, WebSocketAdmissionReason::ClientLimit, false };
        }
        const bool firstSocket = occupiedSlots == 0;
        const auto minimumFree = firstSocket
                                     ? (recoveryEligible ? first_socket_recovery_min_free_bytes : first_socket_min_free_bytes)
                                     : additional_socket_min_free_bytes;
        const auto minimumBlock = occupiedSlots == 0 ? first_socket_min_largest_block : additional_socket_min_largest_block;
        if (freeHeap < minimumFree || largestBlock < minimumBlock) {
            return { false, WebSocketAdmissionReason::HeapReserve, false };
        }
        const bool usedRecovery = firstSocket && recoveryEligible && freeHeap < first_socket_min_free_bytes;
        return { true, WebSocketAdmissionReason::Allowed, usedRecovery };
    }

    constexpr bool file_stream_admission(
        std::size_t freeHeap, std::size_t largestBlock, std::size_t activeStreams, std::size_t activeHeavyResponses = 0) {
        if (activeStreams >= max_active_file_streams || activeHeavyResponses != 0 ||
            largestBlock < file_stream_min_largest_block) {
            return false;
        }
        return freeHeap >= first_file_stream_min_free_bytes;
    }

    constexpr char ascii_upper(char value) {
        return value >= 'a' && value <= 'z' ? char(value - ('a' - 'A')) : value;
    }

    constexpr bool ascii_case_prefix(const char* value, const char* prefix) {
        if (!value || !prefix) {
            return false;
        }
        while (*prefix) {
            if (!*value || ascii_upper(*value) != ascii_upper(*prefix)) {
                return false;
            }
            ++value;
            ++prefix;
        }
        return true;
    }

    constexpr bool is_heavy_http_command(const char* command) {
        if (ascii_case_prefix(command, "[ESP420]")) {
            return true;
        }
        if (!ascii_case_prefix(command, "$ESP420")) {
            return false;
        }
        const char boundary = command[7];
        return boundary == '\0' || boundary == '=' || boundary == '/' || boundary == ' ' || boundary == '\t';
    }

    constexpr bool heavy_http_admission(
        std::size_t freeHeap, std::size_t largestBlock, std::size_t activeFileStreams, std::size_t activeHeavyResponses) {
        return activeFileStreams == 0 && activeHeavyResponses < max_active_heavy_http_responses &&
               freeHeap >= heavy_http_min_free_bytes && largestBlock >= heavy_http_min_largest_block;
    }

    constexpr bool elapsed(uint32_t now, uint32_t then, uint32_t timeout) {
        return int32_t(now - then) > int32_t(timeout);
    }

    constexpr bool first_socket_recovery_timer_mature(uint32_t now, uint32_t zeroSince, bool alreadyMature) {
        return alreadyMature ||
               (zeroSince != 0 && uint32_t(now - zeroSince) > first_socket_recovery_quiet_ms);
    }

    constexpr bool first_socket_recovery_eligible(bool timerMature, std::size_t occupiedSlots) {
        return occupiedSlots == 0 && timerMature;
    }

    constexpr uint32_t first_socket_zero_idle_ms(uint32_t now, uint32_t zeroSince, bool timerMature) {
        if (zeroSince == 0) {
            return 0;
        }
        const auto delta = uint32_t(now - zeroSince);
        if (timerMature) {
            return delta > first_socket_recovery_quiet_ms ? delta : first_socket_recovery_quiet_ms + 1u;
        }
        // A non-mature timer cannot legitimately be more than half a millis()
        // cycle old because the main loop latches it after 60 seconds. Treat
        // such a value as a future timestamp rather than reporting ~49 days.
        return delta > uint32_t(INT32_MAX) ? 0u : delta;
    }

    constexpr bool websocket_pong_expired(uint32_t now, uint32_t lastPongAt) {
        return elapsed(now, lastPongAt, websocket_pong_timeout_ms);
    }
}
