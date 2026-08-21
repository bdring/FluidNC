// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Machine/MachineConfig.h"
#include "Serial.h"    // is_realtime_command()
#include "Settings.h"  // settings_execute_line()
#include "Error.h"     // ErrorException

#include "WebUIServer.h"
#include "WebResourcePolicy.h"

#include "Driver/fluidnc_mdns.h"
#include "NetSettings.h"

#include <WiFi.h>
// #include <StreamString.h>
#ifdef HAVE_UPDATE
#    include <Update.h>
#    include <esp_wifi_types.h>
#    include <esp_ota_ops.h>
#endif
#ifdef HAVE_DNS
#    include <DNSServer.h>
#endif

#include "WSChannel.h"

#include "WebClient.h"

#include "Protocol.h"  // protocol_send_event
#include "FluidPath.h"
#include "JSONEncoder.h"

#include "HashFS.h"
#include <cstdio>
#include <array>
#include <atomic>
#include <list>
#include <algorithm>
#include <limits>
#include <memory>
#include <new>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "Mime.h"  // getContentType

#include <AsyncTCP.h>
#include "WebDAV.h"

#ifdef HAVE_DNS
namespace WebUI {
    const byte DNS_PORT = 53;
    DNSServer  dnsServer;
}
#endif

namespace {
    struct PendingWebSocketSlot {
        AsyncWebServerRequest* request  = nullptr;
        AsyncClient*           transport = nullptr;
        uint32_t               reserved = 0;
    };

    struct ActiveWebSocketSlot {
        uint32_t id         = 0;
        bool     used       = false;
        bool     connecting = false;
    };

    struct DeferredWebSocketClose {
        uint32_t id   = 0;
        bool     used = false;
    };

    portMUX_TYPE web_resource_mux = portMUX_INITIALIZER_UNLOCKED;
    std::array<PendingWebSocketSlot, WebUI::ResourcePolicy::max_websocket_clients> pending_websocket_slots {};
    std::array<ActiveWebSocketSlot, WebUI::ResourcePolicy::max_websocket_clients> active_websocket_slots {};
    std::array<DeferredWebSocketClose, WebUI::ResourcePolicy::max_websocket_clients> deferred_websocket_closes {};
    std::array<WebUI::WebClient*, WebUI::ResourcePolicy::max_websocket_clients> deferred_webclient_kills {};
    size_t active_file_streams = 0;
    size_t active_heavy_http_responses = 0;
    uint32_t websocket_client_limit_rejections = 0;
    uint32_t websocket_heap_rejections         = 0;
    uint32_t websocket_recovery_admissions     = 0;
    uint32_t file_stream_starts                 = 0;
    uint32_t file_stream_completions            = 0;
    uint32_t file_stream_rejections             = 0;
    uint32_t heavy_http_rejections              = 0;
    size_t last_websocket_observed_free          = 0;
    size_t last_websocket_largest_block          = 0;
    size_t last_websocket_effective_free         = 0;
    size_t last_websocket_occupied_slots         = 0;
    uint32_t websocket_zero_occupancy_since      = 0;
    bool websocket_zero_occupancy_mature         = false;
    std::atomic<uint32_t> firmware_ota_active { 0 };
    std::atomic<uint32_t> firmware_ota_expected_bytes { 0 };
    std::atomic<uint32_t> firmware_ota_accepted_bytes { 0 };
    std::atomic<uint32_t> firmware_ota_max_write_us { 0 };
    std::atomic<uint32_t> firmware_ota_disconnect_aborts { 0 };
    std::atomic<uint32_t> firmware_ota_failures { 0 };
    std::atomic<uint32_t> firmware_ota_failure_recorded { 0 };
    std::atomic<uint32_t> firmware_ota_update_started { 0 };

    class WebResourceLock {
    public:
        WebResourceLock() { portENTER_CRITICAL(&web_resource_mux); }
        ~WebResourceLock() { portEXIT_CRITICAL(&web_resource_mux); }

        WebResourceLock(const WebResourceLock&) = delete;
        WebResourceLock& operator=(const WebResourceLock&) = delete;
    };

    size_t subtract_reservation(size_t available, size_t reserved) { return available > reserved ? available - reserved : 0; }

    void increment_saturating(uint32_t& value) {
        if (value < static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            ++value;
        }
    }

    void update_max(std::atomic<uint32_t>& destination, uint32_t value) {
        uint32_t observed = destination.load(std::memory_order_relaxed);
        while (observed < value &&
               !destination.compare_exchange_weak(observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    void record_firmware_ota_failure() {
        if (firmware_ota_failure_recorded.exchange(1, std::memory_order_relaxed) == 0) {
            firmware_ota_failures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void expire_pending_websocket_slots_locked(uint32_t now) {
        for (auto& slot : pending_websocket_slots) {
            if (slot.request && WebUI::ResourcePolicy::elapsed(now, slot.reserved, WebUI::ResourcePolicy::pending_socket_timeout_ms)) {
                slot = {};
            }
        }
    }

    size_t pending_websocket_count_locked() {
        size_t count = 0;
        for (const auto& slot : pending_websocket_slots) {
            count += slot.request != nullptr;
        }
        return count;
    }

    size_t active_websocket_count_locked() {
        size_t count = 0;
        for (const auto& slot : active_websocket_slots) {
            count += slot.used;
        }
        return count;
    }

    size_t connecting_websocket_count_locked() {
        size_t count = 0;
        for (const auto& slot : active_websocket_slots) {
            count += slot.used && slot.connecting;
        }
        return count;
    }

    size_t untracked_deferred_close_count_locked() {
        size_t count = 0;
        for (const auto& deferred : deferred_websocket_closes) {
            if (!deferred.used) {
                continue;
            }
            bool tracked = false;
            for (const auto& active : active_websocket_slots) {
                tracked = tracked || (active.used && active.id == deferred.id);
            }
            count += !tracked;
        }
        return count;
    }

    size_t deferred_websocket_count_locked() {
        size_t count = 0;
        for (const auto& deferred : deferred_websocket_closes) {
            count += deferred.used;
        }
        return count;
    }

    size_t websocket_occupied_count_locked() {
        return pending_websocket_count_locked() + active_websocket_count_locked() + untracked_deferred_close_count_locked();
    }

    void note_websocket_occupancy_locked(uint32_t now) {
        if (websocket_occupied_count_locked() == 0) {
            if (websocket_zero_occupancy_since == 0) {
                websocket_zero_occupancy_since = now == 0 ? UINT32_MAX : now;
                websocket_zero_occupancy_mature = false;
            }
            websocket_zero_occupancy_mature = WebUI::ResourcePolicy::first_socket_recovery_timer_mature(
                now, websocket_zero_occupancy_since, websocket_zero_occupancy_mature);
        } else {
            websocket_zero_occupancy_since = 0;
            websocket_zero_occupancy_mature = false;
        }
    }

    bool try_reserve_websocket_slot(
        AsyncWebServerRequest* request, size_t freeHeap, size_t largestBlock, bool replacesSession) {
        if (!request) {
            return false;
        }

        WebResourceLock lock;
        const auto now = millis();
        expire_pending_websocket_slots_locked(now);
        const auto pending = pending_websocket_count_locked();
        const auto deferred = untracked_deferred_close_count_locked();
        const auto occupied = pending + active_websocket_count_locked() + deferred;
        note_websocket_occupancy_locked(now);
        const auto existingSocketReservations = pending + connecting_websocket_count_locked() + deferred;
        const auto reserved = existingSocketReservations * WebUI::ResourcePolicy::additional_socket_reservation_bytes +
                              WebUI::ResourcePolicy::pending_socket_reservation(occupied, replacesSession) +
                              active_file_streams * WebUI::ResourcePolicy::file_stream_reservation_bytes +
                              active_heavy_http_responses * WebUI::ResourcePolicy::heavy_http_reservation_bytes;
        const auto effectiveFree = subtract_reservation(freeHeap, reserved);
        last_websocket_observed_free  = freeHeap;
        last_websocket_largest_block  = largestBlock;
        last_websocket_effective_free = effectiveFree;
        last_websocket_occupied_slots = occupied;
        const auto recoveryEligible = WebUI::ResourcePolicy::first_socket_recovery_eligible(
            websocket_zero_occupancy_mature, occupied);
        const auto admission = WebUI::ResourcePolicy::websocket_admission(effectiveFree, largestBlock, occupied, recoveryEligible);
        if (!admission.allowed) {
            if (admission.reason == WebUI::ResourcePolicy::WebSocketAdmissionReason::ClientLimit) {
                increment_saturating(websocket_client_limit_rejections);
            } else {
                increment_saturating(websocket_heap_rejections);
            }
            return false;
        }

        for (auto& slot : pending_websocket_slots) {
            if (!slot.request) {
                slot = { request, request->client(), now };
                websocket_zero_occupancy_since = 0;
                websocket_zero_occupancy_mature = false;
                if (admission.usedRecovery) {
                    increment_saturating(websocket_recovery_admissions);
                }
                return true;
            }
        }
        return false;
    }

    bool promote_websocket_slot(AsyncWebServerRequest* request, AsyncClient* transport, uint32_t clientId) {
        WebResourceLock lock;
        const auto now = millis();
        expire_pending_websocket_slots_locked(now);

        PendingWebSocketSlot* pending = nullptr;
        for (auto& slot : pending_websocket_slots) {
            if (slot.request == request && slot.transport == transport) {
                pending = &slot;
                break;
            }
        }
        if (!pending) {
            return false;
        }

        for (auto& slot : active_websocket_slots) {
            if (!slot.used) {
                slot    = { clientId, true, true };
                *pending = {};
                return true;
            }
        }
        return false;
    }

    void complete_websocket_slot(uint32_t clientId) {
        WebResourceLock lock;
        for (auto& slot : active_websocket_slots) {
            if (slot.used && slot.id == clientId) {
                slot.connecting = false;
                return;
            }
        }
    }

    void release_websocket_slot(uint32_t clientId) {
        WebResourceLock lock;
        const auto now = millis();
        for (auto& slot : active_websocket_slots) {
            if (slot.used && slot.id == clientId) {
                slot = {};
                break;
            }
        }
        for (auto& deferred : deferred_websocket_closes) {
            if (deferred.used && deferred.id == clientId) {
                deferred = {};
            }
        }
        note_websocket_occupancy_locked(now);
    }

    void schedule_websocket_close(uint32_t clientId) {
        WebResourceLock lock;
        for (const auto& deferred : deferred_websocket_closes) {
            if (deferred.used && deferred.id == clientId) {
                return;
            }
        }
        for (auto& deferred : deferred_websocket_closes) {
            if (!deferred.used) {
                deferred = { clientId, true };
                return;
            }
        }
    }

    void expire_pending_websocket_slots() {
        WebResourceLock lock;
        const auto now = millis();
        expire_pending_websocket_slots_locked(now);
        note_websocket_occupancy_locked(now);
    }

    bool try_reserve_file_stream(size_t freeHeap, size_t largestBlock) {
        WebResourceLock lock;
        const auto socketReservations = pending_websocket_count_locked() + connecting_websocket_count_locked() +
                                        untracked_deferred_close_count_locked();
        const auto reserved = socketReservations * WebUI::ResourcePolicy::additional_socket_reservation_bytes;
        const auto effectiveFree = subtract_reservation(freeHeap, reserved);
        if (!WebUI::ResourcePolicy::file_stream_admission(
                effectiveFree, largestBlock, active_file_streams, active_heavy_http_responses)) {
            increment_saturating(file_stream_rejections);
            return false;
        }
        ++active_file_streams;
        increment_saturating(file_stream_starts);
        return true;
    }

    void release_file_stream() {
        WebResourceLock lock;
        if (active_file_streams) {
            --active_file_streams;
            increment_saturating(file_stream_completions);
        }
    }

    bool try_reserve_heavy_http_response(size_t freeHeap, size_t largestBlock) {
        WebResourceLock lock;
        const auto socketReservations = pending_websocket_count_locked() + connecting_websocket_count_locked() +
                                        untracked_deferred_close_count_locked();
        const auto reserved = socketReservations * WebUI::ResourcePolicy::additional_socket_reservation_bytes;
        const auto effectiveFree = subtract_reservation(freeHeap, reserved);
        if (!WebUI::ResourcePolicy::heavy_http_admission(
                effectiveFree, largestBlock, active_file_streams, active_heavy_http_responses)) {
            increment_saturating(heavy_http_rejections);
            return false;
        }
        ++active_heavy_http_responses;
        return true;
    }

    void release_heavy_http_response() {
        WebResourceLock lock;
        if (active_heavy_http_responses) {
            --active_heavy_http_responses;
        }
    }

    class HeavyHttpReservation {
    public:
        explicit HeavyHttpReservation(bool required) : _held(!required || try_reserve_heavy_http_response(
                                                                           heap_caps_get_free_size(MALLOC_CAP_8BIT),
                                                                           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))),
                                                       _ownsSlot(required && _held) {}
        ~HeavyHttpReservation() {
            if (_ownsSlot) {
                release_heavy_http_response();
            }
        }

        explicit operator bool() const { return _held; }
        void transfer() { _ownsSlot = false; }

        HeavyHttpReservation(const HeavyHttpReservation&) = delete;
        HeavyHttpReservation& operator=(const HeavyHttpReservation&) = delete;

    private:
        bool _held;
        bool _ownsSlot;
    };

    class FileStreamReservation {
    public:
        FileStreamReservation() : _held(try_reserve_file_stream(
                                      heap_caps_get_free_size(MALLOC_CAP_8BIT),
                                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))) {}
        ~FileStreamReservation() {
            if (_held) {
                release_file_stream();
            }
        }

        explicit operator bool() const { return _held; }
        void transfer() { _held = false; }

        FileStreamReservation(const FileStreamReservation&) = delete;
        FileStreamReservation& operator=(const FileStreamReservation&) = delete;

    private:
        bool _held;
    };

    struct ActiveFileStream {
        FileStream* stream    = nullptr;
        bool        owns_slot = false;

        ~ActiveFileStream() {
            delete stream;
            if (owns_slot) {
                release_file_stream();
            }
        }
    };

    void close_request_client(AsyncWebServerRequest* request) {
        if (request && request->client()) {
            request->client()->close();
        }
    }

    void abort_request_client(AsyncWebServerRequest* request) {
        if (request && request->client()) {
            request->client()->abort();
        }
    }

    bool schedule_deferred_webclient_kill(WebUI::WebClient* client) {
        if (!client) {
            return true;
        }
        WebResourceLock lock;
        for (auto* deferred : deferred_webclient_kills) {
            if (deferred == client) {
                return true;
            }
        }
        for (auto& deferred : deferred_webclient_kills) {
            if (!deferred) {
                deferred = client;
                return true;
            }
        }
        return false;
    }

    void process_deferred_webclient_kills() {
        std::array<WebUI::WebClient*, WebUI::ResourcePolicy::max_websocket_clients> pending {};
        {
            WebResourceLock lock;
            pending = deferred_webclient_kills;
        }
        for (auto* client : pending) {
            if (!client || !allChannels.kill(client)) {
                continue;
            }
            WebResourceLock lock;
            for (auto& deferred : deferred_webclient_kills) {
                if (deferred == client) {
                    deferred = nullptr;
                    break;
                }
            }
        }
    }

    void process_deferred_websocket_closes(AsyncWebSocket* server) {
        std::array<uint32_t, WebUI::ResourcePolicy::max_websocket_clients> clientIds {};
        size_t clientCount = 0;
        {
            WebResourceLock lock;
            for (const auto& deferred : deferred_websocket_closes) {
                if (deferred.used) {
                    clientIds[clientCount++] = deferred.id;
                }
            }
        }

        for (size_t index = 0; index < clientCount; ++index) {
            const auto clientId = clientIds[index];
            if (server) {
                try {
                    server->close(clientId);
                } catch (const std::bad_alloc&) {
                    // Keep the client and its resource slot accounted for. A later
                    // poll retries without dereferencing a client pointer outside
                    // the WebSocket server's internal lock.
                }
            }
        }
    }

    struct FileListChunkState {
        enum class Phase : uint8_t { Begin, FileEntries, Footer, End, Done };

        explicit FileListChunkState(
            FluidPath root,
            std::string request_path,
            std::string response_status,
            std::string total_bytes,
            std::string used_bytes,
            uint8_t occupation_percent) :
            root_path(std::move(root)),
            path(std::move(request_path)),
            status(std::move(response_status)),
            total(std::move(total_bytes)),
            used(std::move(used_bytes)),
            percent(occupation_percent),
            encoder([this](const char* s) { pending += s; }) {}

        FileListChunkState(const FileListChunkState&) = delete;
        FileListChunkState& operator=(const FileListChunkState&) = delete;

        Phase                     phase          = Phase::Begin;
        stdfs::directory_iterator iter;
        stdfs::directory_iterator end;
        FluidPath                 root_path;
        std::string               path;
        std::string               status;
        std::string               total;
        std::string               used;
        std::string               pending;
        size_t                    pending_offset = 0;
        uint8_t                   percent        = 100;
        bool                      emit_files     = false;
        JSONencoder               encoder;
    };

    int32_t file_entry_size(const stdfs::directory_entry& dir_entry) {
        std::error_code ec;

        if (dir_entry.is_directory(ec) || ec) {
            return -1;
        }

        ec = {};
        if (!dir_entry.is_regular_file(ec) || ec) {
            return -1;
        }

        ec = {};
        auto entry_size = dir_entry.file_size(ec);
        if (ec || entry_size == static_cast<uintmax_t>(-1)) {
            return -1;
        }

        return static_cast<int32_t>(entry_size);
    }

    void advance_file_iterator(FileListChunkState& state) {
        std::error_code ec;
        state.iter.increment(ec);
        if (ec) {
            state.iter = state.end;
        }
    }

    void append_file_entry(FileListChunkState& state) {
        const auto& dir_entry = *state.iter;
        std::string name      = dir_entry.path().filename().string();
        int32_t     size      = file_entry_size(dir_entry);

        state.encoder.begin_object();
        state.encoder.member("name", name);
        state.encoder.member("shortname", name);
        state.encoder.member("size", size);
        state.encoder.member("datetime", "");
        state.encoder.end_object();
        advance_file_iterator(state);
    }

    bool advance_file_list_chunk(FileListChunkState& state) {
        switch (state.phase) {
            case FileListChunkState::Phase::Begin:
                state.encoder.begin();
                if (state.emit_files) {
                    state.encoder.begin_array("files");
                    state.phase = FileListChunkState::Phase::FileEntries;
                } else {
                    state.phase = FileListChunkState::Phase::Footer;
                }
                state.encoder.flush();
                return true;

            case FileListChunkState::Phase::FileEntries:
                if (state.iter == state.end) {
                    state.encoder.end_array();
                    state.phase = FileListChunkState::Phase::Footer;
                } else {
                    append_file_entry(state);
                }
                state.encoder.flush();
                return true;

            case FileListChunkState::Phase::Footer:
                state.encoder.member("path", state.path.c_str());
                state.encoder.member("total", state.total.c_str());
                state.encoder.member("used", state.used.c_str());
                state.encoder.member("occupation", state.percent);
                state.encoder.member("status", state.status.c_str());
                state.phase = FileListChunkState::Phase::End;
                state.encoder.flush();
                return true;

            case FileListChunkState::Phase::End:
                state.encoder.end();
                state.phase = FileListChunkState::Phase::Done;
                return true;

            case FileListChunkState::Phase::Done:
                return false;
        }

        return false;
    }

    AsyncWebServerResponse* create_file_list_response(AsyncWebServerRequest* request,
                                                      const FluidPath&          fpath,
                                                      const std::string&        path,
                                                      const std::string&        status,
                                                      bool                      list_files) {
        std::error_code ec;
        auto            space      = stdfs::space(fpath, ec);
        uint64_t        totalspace = space.capacity;
        uint64_t        usedspace  = totalspace - space.available;
        uint8_t         percent    = totalspace ? (usedspace * 100) / totalspace : 100;

        auto state = std::make_shared<FileListChunkState>(
            fpath,
            path,
            status,
            formatBytes(totalspace),
            formatBytes(usedspace + 1),
            percent);

        if (list_files) {
            state->iter       = stdfs::directory_iterator { fpath, stdfs::directory_options::skip_permission_denied, ec };
            state->emit_files = !ec;
        }

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            asyncsrv::T_application_json,
            [state](uint8_t* buffer, size_t maxLen, size_t total) mutable -> size_t {
                (void)total;

                size_t written = 0;

                while (written < maxLen) {
                    if (state->pending_offset < state->pending.length()) {
                        size_t chunk_len = std::min(maxLen - written, state->pending.length() - state->pending_offset);
                        memcpy(buffer + written, state->pending.data() + state->pending_offset, chunk_len);
                        state->pending_offset += chunk_len;
                        written += chunk_len;
                        continue;
                    }

                    state->pending.clear();
                    state->pending_offset = 0;

                    if (!advance_file_list_chunk(*state)) {
                        break;
                    }
                }

                return written;
            });
        response->addHeader(asyncsrv::T_Cache_Control, asyncsrv::T_no_cache);
        return response;
    }
}

extern "C" uint32_t fluidnc_ota_active() { return firmware_ota_active.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_expected_bytes() { return firmware_ota_expected_bytes.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_accepted_bytes() { return firmware_ota_accepted_bytes.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_max_write_us() { return firmware_ota_max_write_us.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_disconnect_aborts() { return firmware_ota_disconnect_aborts.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_failures() { return firmware_ota_failures.load(std::memory_order_relaxed); }
extern "C" uint32_t fluidnc_ota_update_owned() { return firmware_ota_update_started.load(std::memory_order_relaxed); }

namespace WebUI::ResourcePolicy {
    RuntimeSnapshot runtime_snapshot() {
        WebResourceLock lock;
        const auto now = millis();
        const auto zeroIdleMs = first_socket_zero_idle_ms(
            now, websocket_zero_occupancy_since, websocket_zero_occupancy_mature);
        return {
            pending_websocket_count_locked(),
            active_websocket_count_locked(),
            connecting_websocket_count_locked(),
            deferred_websocket_count_locked(),
            active_file_streams,
            active_heavy_http_responses,
            websocket_client_limit_rejections,
            websocket_heap_rejections,
            websocket_recovery_admissions,
            file_stream_starts,
            file_stream_completions,
            file_stream_rejections,
            heavy_http_rejections,
            last_websocket_observed_free,
            last_websocket_largest_block,
            last_websocket_effective_free,
            last_websocket_occupied_slots,
            zeroIdleMs,
        };
    }
}

using namespace asyncsrv;

//embedded response file if no files on LocalFS
#include "NoFile.h"

namespace WebUI {
    // Error codes for upload
    const int ESP_ERROR_AUTHENTICATION   = 1;
    const int ESP_ERROR_FILE_CREATION    = 2;
    const int ESP_ERROR_FILE_WRITE       = 3;
    const int ESP_ERROR_UPLOAD           = 4;
    const int ESP_ERROR_NOT_ENOUGH_SPACE = 5;
    const int ESP_ERROR_UPLOAD_CANCELLED = 6;
    const int ESP_ERROR_FILE_CLOSE       = 7;

    static const char LOCATION_HEADER[] = "Location";

    bool     WebUI_Server::_setupdone            = false;
    uint16_t WebUI_Server::_port                 = 0;
    bool     WebUI_Server::_schedule_reboot      = false;
    uint32_t WebUI_Server::_schedule_reboot_time = 0;

    UploadStatus               WebUI_Server::_upload_status            = UploadStatus::NONE;
    AsyncWebServerRequest*     WebUI_Server::_firmware_upload_request  = nullptr;
    AsyncWebServer*            WebUI_Server::_webserver       = NULL;
    AsyncWebServer*            WebUI_Server::_websocketserver = NULL;
    AsyncHeaderFreeMiddleware* WebUI_Server::_headerFilter    = NULL;
    AsyncWebSocket*            WebUI_Server::_socket_server   = NULL;
#ifdef ENABLE_AUTHENTICATION
    AuthenticationIP* WebUI_Server::_head  = NULL;
    uint8_t           WebUI_Server::_nb_ip = 0;
    const int         MAX_AUTH_IP          = 10;
#endif
    FileStream* WebUI_Server::_uploadFile = nullptr;
    std::string WebUI_Server::_uploadPath = "";  // Store upload directory path for listing

    EnumSetting *http_enable, *http_block_during_motion;
    IntSetting*  http_port;

    WebUI_Server::~WebUI_Server() {
        deinit();
    }

    void WebUI_Server::init() {
        http_port   = new IntSetting("HTTP Port", WEBSET, WA, "ESP121", "HTTP/Port", DEFAULT_HTTP_PORT, MIN_HTTP_PORT, MAX_HTTP_PORT);
        http_enable = new EnumSetting("HTTP Enable", WEBSET, WA, "ESP120", "HTTP/Enable", DEFAULT_HTTP_STATE, &onoffOptions);
        http_block_during_motion = new EnumSetting("Block serving HTTP content during motion",
                                                   WEBSET,
                                                   WA,
                                                   NULL,
                                                   "HTTP/BlockDuringMotion",
                                                   DEFAULT_HTTP_BLOCKED_DURING_MOTION,
                                                   &onoffOptions);

        _setupdone = false;

        if (!networkEnabled() || !http_enable->get()) {
            return;
        }

        _port = http_port->get();

        //create instance
        _webserver    = new AsyncWebServer(_port);
        _headerFilter = new AsyncHeaderFreeMiddleware();

        //here the list of headers to be recorded
        _headerFilter->keep("Accept");
        _headerFilter->keep("Accept-Encoding");
        _headerFilter->keep("Cookie");
        _headerFilter->keep("If-None-Match");
        _headerFilter->keep("User-Agent");

        //For websockets we need to keep these headers, otherwise this wouldn't work!
        _headerFilter->keep("Upgrade");
        _headerFilter->keep("Connection");
        _headerFilter->keep("Sec-WebSocket-Key");
        _headerFilter->keep("Sec-WebSocket-Version");
        _headerFilter->keep("Sec-WebSocket-Protocol");
        _headerFilter->keep("Sec-WebSocket-Extensions");

        // WebDAV needs these
        _headerFilter->keep("Depth");
        _headerFilter->keep("Destination");

        _webserver->addMiddlewares({ _headerFilter });

        // No metadata on the FLASH filesystem; it consumes too much space
        auto flash_dav = new WebDAV("/flash", LocalFS, true);
        auto sd_dav    = new WebDAV("/sd", SD, true);

        _webserver->addHandler(flash_dav);
        _webserver->addHandler(sd_dav);

        // The only major difference with websockets for v2 webui vs v3 seems to be the currentID vs CURRENT_ID and activeID vs ACTIVE_ID
        // In order to only have one websocket server (for simplicity and maintability reasons) we could:
        // 1 - Send both messages types all the time
        // 2 - Don't do anything and just send v3 payloads, since at this point it seems we don't rely on this pageId mechanism anymore with
        // our async and cookie session implementation
        // 3 - Remove all of these active and current IDs altogether since again, we may have no need for this anymore
        // 4 - Potentially check for a difference in requests headers of v2 vs v3 to dynamically send the proper payload in the same handler
        // For now, I've settled with #3
        _socket_server = new AsyncWebSocket("/");
        _socket_server->handleHandshake([](AsyncWebServerRequest* request) {
            bool replacesSession = false;
            try {
                // A reconnect from the same browser session is a replacement, not
                // additional capacity. Abort its older transport before admission
                // so a stale socket cannot consume the heap needed to replace it.
                replacesSession = WSChannels::abortSessionChannels(WebUI_Server::getWebSocketSession(request));
            } catch (const std::bad_alloc&) {
                // This handler returns false to the patched ESPAsyncWebServer
                // fail-closed abort path.  Attribute the exceptional admission
                // refusal too, otherwise a client can observe a transport close
                // without a matching resource-policy counter.
                WebResourceLock lock;
                increment_saturating(websocket_heap_rejections);
                return false;
            }
            return try_reserve_websocket_slot(
                request,
                heap_caps_get_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                replacesSession);
        });
        _socket_server->onEvent(
            [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
                const auto clientId = client ? client->id() : 0;
                if (type == WS_EVT_CONNECT) {
                    auto* request = static_cast<AsyncWebServerRequest*>(arg);
                    if (!client || !promote_websocket_slot(request, client ? client->client() : nullptr, clientId)) {
                        if (client) {
                            schedule_websocket_close(clientId);
                        }
                        return;
                    }
                } else if (type == WS_EVT_DISCONNECT) {
                    release_websocket_slot(clientId);
                }

                try {
                    WSChannels::handleEvent(server, client, type, arg, data, len);
                    if (type == WS_EVT_CONNECT) {
                        complete_websocket_slot(clientId);
                    }
                } catch (const std::bad_alloc&) {
                    if (type != WS_EVT_DISCONNECT) {
                        WSChannels::removeChannel(clientId);
                        schedule_websocket_close(clientId);
                    } else {
                        release_websocket_slot(clientId);
                    }
                }
            });

        _webserver->addHandler(_socket_server);

        //events functions
        //_web_events->onConnect(handle_onevent_connect);
        //events management
        // _webserver->addHandler(_web_events);

        //Web server handlers
        //trick to catch command line on "/" before file being processed
        _webserver->on("/", HTTP_ANY, handle_root);

        //Page not found handler
        _webserver->onNotFound(handle_not_found);

        //need to be there even no authentication to say to UI no authentication
        _webserver->on("/login", HTTP_ANY, handle_login);

        //web commands
        _webserver->on("/command", HTTP_ANY, handle_web_command);
        _webserver->on("/command_silent", HTTP_ANY, handle_web_command_silent);
        _webserver->on("/trace", HTTP_ANY, handle_trace);
        _webserver->on("/feedhold_reload", HTTP_ANY, handleFeedholdReload);
        _webserver->on("/cyclestart_reload", HTTP_ANY, handleCyclestartReload);
        _webserver->on("/restart_reload", HTTP_ANY, handleRestartReload);
        _webserver->on("/did_restart", HTTP_ANY, handleDidRestart);

        //LocalFS
        _webserver->on("/files", HTTP_ANY, handleFileList, LocalFSFileupload);

#ifdef HAVE_UPDATE
        //web update
        _webserver->on("/updatefw", HTTP_POST, handleUpdate, WebUpdateUpload);
#endif

        //Direct SD management
        _webserver->on("/upload", HTTP_ANY, handle_direct_SDFileList, SDFileUpload);
        //_webserver->on("/SD", HTTP_ANY, handle_SDCARD);

#ifdef HAVE_DNS
        if (WiFi.getMode() == WIFI_AP) {
            // if DNSServer is started with "*" for domain name, it will reply with
            // provided IP to all DNS request
            dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
            log_info("Captive Portal Started");
            _webserver->on("/generate_204", HTTP_ANY, handle_root);
            _webserver->on("/gconnectivitycheck.gstatic.com", HTTP_ANY, handle_root);
            //do not forget the / at the end
            _webserver->on("/fwlink/", HTTP_ANY, handle_root);
        }
        Mdns::add("_http", "_tcp", _port);
#endif

        log_info("HTTP started on port " << WebUI::http_port->get());
        //start webserver
        _webserver->begin();

        HashFS::hash_all();

        _setupdone = true;
    }

    void WebUI_Server::deinit() {
        _setupdone = false;

        //        SSDP.end();

#ifdef HAVE_DNS
        WMB Mdns::remove("_http", "_tcp");
#endif

        if (_socket_server) {
            delete _socket_server;
            _socket_server = NULL;
        }

        if (_webserver) {
            delete _webserver;
            _webserver = NULL;
        }

        if (_websocketserver) {
            delete _websocketserver;
            _websocketserver = NULL;
        }

        if (_headerFilter) {
            delete _headerFilter;
            _headerFilter = NULL;
        }

#ifdef ENABLE_AUTHENTICATION
        while (_head) {
            AuthenticationIP* current = _head;
            _head                     = _head->_next;
            delete current;
        }
        _nb_ip = 0;
#endif
    }

    std::string WebUI_Server::getSessionCookie(AsyncWebServerRequest* request) {
        if (request->hasHeader("Cookie")) {
            std::string cookies = request->getHeader("Cookie")->value().c_str();

            int pos = cookies.find("sessionId=");
            if (pos != std::string::npos) {
                int pos2 = cookies.find(";", pos);
                auto start = pos + strlen("sessionId=");
                if (pos2 == std::string::npos) {
                    return cookies.substr(start);
                }
                return cookies.substr(start, pos2 - start);
            }
        }
        return "";
    }

    std::string WebUI_Server::getWebSocketSession(AsyncWebServerRequest* request, AsyncWebSocketClient* client) {
        if (request->hasParam("independent_session")) {
            return client ? getSession(client) : getSession(request->client());
        }
        return getSessionCookie(request);
    }

    static void get_random_string(char* str, unsigned int len) {
        unsigned int i;

        // reseed the random number generator
        srand(time(NULL));

        for (i = 0; i < len; i++) {
            // Add random printable ASCII char
            str[i] = (rand() % ('A' - 'Z')) + 'A';
        }
        str[i] = '\0';
    }
    // Send a file, either the specified path or path.gz
    bool WebUI_Server::myStreamFile(AsyncWebServerRequest* request, const char* path, bool download, bool setSession) {
        try {
            // Reserve before constructing paths, parsing headers, or hashing the
            // file. Those operations allocate too and must not run concurrently
            // outside the same bound that protects the FileStream response.
            FileStreamReservation reservation;
            if (!reservation) {
                close_request_client(request);
                return true;
            }

            std::error_code ec;
            FluidPath       fpath { path, LocalFS, ec };
            if (ec) {
                return false;
            }

            bool acceptGz = false;
            if (request->hasHeader("Accept-Encoding")) {
                auto encodings = std::string(request->getHeader("Accept-Encoding")->value().c_str());
                if (encodings.find("gzip") != std::string::npos) {
                    acceptGz = true;
                }
            }

            std::string hash;

            // If you load or reload WebUI while a program is running, there is a high
            // risk of stalling the motion because serving a file from
            // the local FLASH filesystem takes away a lot of CPU cycles.  If we get
            // a request for a file when running, reject it to preserve the motion
            // integrity.
            // This can make it hard to debug ISR IRAM problems, because the easiest
            // way to trigger such problems is to refresh WebUI during motion.
            if (http_block_during_motion->get() && inMotionState()) {
                // Check to see if we have a cached hash of the file that can be retrieved without accessing FLASH
                hash = HashFS::hash(fpath, true);
                if (!hash.length() && acceptGz) {
                    std::filesystem::path gzpath(fpath);
                    gzpath += ".gz";
                    hash = HashFS::hash(gzpath, true);
                }

                if (hash.length() && request->hasHeader("If-None-Match") &&
                    std::string(request->getHeader("If-None-Match")->value().c_str()) == hash) {
                    request->send(304);
                    return true;
                }

                WebUI_Server::handleReloadBlocked(request);
                return true;
            }

            // Check for browser cache match
            hash = HashFS::hash(fpath);
            if (!hash.length() && acceptGz) {
                std::filesystem::path gzpath(fpath);
                gzpath += ".gz";
                hash = HashFS::hash(gzpath);
            }
            if (hash.length() && request->hasHeader("If-None-Match") &&
                std::string(request->getHeader("If-None-Match")->value().c_str()) == hash) {
                if (setSession && getSessionCookie(request) == "") {
                    char session[9];
                    get_random_string(session, sizeof(session) - 1);
                    std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(304));
                    response->addHeader("Set-Cookie", ("sessionId=" + std::string(session)).c_str());
                    auto* rawResponse = response.get();
                    request->send(rawResponse);
                    if (request->getResponse() == rawResponse) {
                        response.release();
                    }
                } else {
                    request->send(304);
                }
                return true;
            }

            auto file       = std::make_shared<ActiveFileStream>();
            file->owns_slot = true;
            reservation.transfer();

            bool isGzip = false;
            try {
                file->stream = new FileStream(fpath, "r", LocalFS);
            } catch (const ErrorException&) {
                if (acceptGz) {
                    try {
                        fpath += ".gz";
                        file->stream = new FileStream(fpath, "r");
                        isGzip      = true;
                    } catch (const ErrorException&) {}
                }
            }
            if (!file->stream) {
                log_debug(path << " not found");
                return false;
            }

            std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(
                getContentType(path), file->stream->size(), [file, request](uint8_t* buffer, size_t maxLen, size_t total) -> size_t {
                    auto* stream = file->stream;
                    if (!stream) {
                        close_request_client(request);
                        return 0;  // RESPONSE_TRY_AGAIN only works for a chunked response.
                    }
                    if (total >= stream->size() || request->method() != HTTP_GET) {
                        return 0;
                    }
                    size_t bytes  = min(stream->size() - total, maxLen);
                    int    actual = stream->read(buffer, bytes);  // Returns 0 if no bytes were loaded.
                    return actual;
                }));
            if (!response) {
                close_request_client(request);
                return true;
            }

            if (setSession && getSessionCookie(request) == "") {
                char session[9];
                get_random_string(session, sizeof(session) - 1);
                response->addHeader("Set-Cookie", ("sessionId=" + std::string(session)).c_str());
            }
            if (download) {
                response->addHeader("Content-Disposition", "attachment");
            }
            if (hash.length()) {
                response->addHeader("ETag", hash.c_str());
            }
            if (isGzip) {
                response->addHeader(T_Content_Encoding, T_gzip);
            }

            auto* rawResponse = response.get();
            request->send(rawResponse);
            if (request->getResponse() == rawResponse) {
                response.release();
            } else {
                close_request_client(request);
            }
            return true;
        } catch (const std::bad_alloc&) {
            close_request_client(request);
            return true;
        }
    }
    void WebUI_Server::sendWithOurAddress(AsyncWebServerRequest* request, const char* content, uint16_t code) {
        std::string ipstr = webServerIp();
        if (_port != 80) {
            ipstr += ":";
            ipstr += std::to_string(_port);
        }

        std::string scontent(content);
        replace_string_in_place(scontent, "$WEB_ADDRESS$", ipstr);
        replace_string_in_place(scontent, "$QUERY$", request->url().c_str());
        request->send(code, "text/html", scontent.c_str());
    }

    // Captive Portal Page for use in AP mode
    const char PAGE_CAPTIVE[] =
        "<HTML>\n<HEAD>\n<title>Captive Portal</title> \n</HEAD>\n<BODY>\n<CENTER>Captive Portal page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void WebUI_Server::sendCaptivePortal(AsyncWebServerRequest* request) {
        sendWithOurAddress(request, PAGE_CAPTIVE, 200);
    }

    //Default 404 page that is sent when a request cannot be satisfied
    const char PAGE_404[] =
        "<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void WebUI_Server::send404Page(AsyncWebServerRequest* request) {
        sendWithOurAddress(request, PAGE_404, 404);
    }

    void WebUI_Server::handle_root(AsyncWebServerRequest* request) {
        log_info("WebUI: Request from " << request->client()->remoteIP());
        const char* referer    = request->hasHeader("Referer") ? request->getHeader("Referer")->value().c_str() : "";
        const char* fetch_mode = request->hasHeader("Sec-Fetch-Mode") ? request->getHeader("Sec-Fetch-Mode")->value().c_str() : "";
        const char* fetch_dest = request->hasHeader("Sec-Fetch-Dest") ? request->getHeader("Sec-Fetch-Dest")->value().c_str() : "";
        const char* fetch_site = request->hasHeader("Sec-Fetch-Site") ? request->getHeader("Sec-Fetch-Site")->value().c_str() : "";
        auto session = getSessionCookie(request);
        if (!session.empty()) {
            WSChannels::closeSessionChannels(session);
        }
        if (!(request->hasParam("forcefallback") && request->getParam("forcefallback")->value() == "yes")) {
            if (myStreamFile(request, "index.html", false, true)) {
                return;
            }
        }

        // If we did not send index.html, send the default content that provides simple localfs file management
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", (const uint8_t*)PAGE_NOFILES, PAGE_NOFILES_SIZE);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void WebUI_Server::handle_trace(AsyncWebServerRequest* request) {
        std::string msg;
        if (request->hasParam("msg")) {
            msg = request->getParam("msg")->value().c_str();
        }
        std::printf("[WEBUI_BROWSER] session=%s pageid=%lu msg=%s\n",
                    getSessionCookie(request).c_str(),
                    (unsigned long)getPageid(request),
                    msg.c_str());
        request->send(200, "text/plain", "");
    }

    // Handle filenames and other things that are not explicitly registered
    void WebUI_Server::handle_not_found(AsyncWebServerRequest* request) {
        const char* upgrade    = request->hasHeader("Upgrade") ? request->getHeader("Upgrade")->value().c_str() : "";
        const char* connection = request->hasHeader("Connection") ? request->getHeader("Connection")->value().c_str() : "";
        const char* protocol =
            request->hasHeader("Sec-WebSocket-Protocol") ? request->getHeader("Sec-WebSocket-Protocol")->value().c_str() : "";
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            request->redirect("/");
            //_webserver->client().stop();
            return;
        }

        const auto& path = request->url();
        if (path.startsWith("/api/")) {
            request->send(404);
            return;
        }

        // Download a file.  The true forces a download instead of displaying the file
        if (myStreamFile(request, path.c_str(), true)) {
            return;
        }

        if (WiFi.getMode() == WIFI_AP) {
            sendCaptivePortal(request);
            return;
        }

        // This lets the user customize the not-found page by
        // putting a "404.htm" file on the local filesystem
        if (myStreamFile(request, "404.htm")) {
            return;
        }

        send404Page(request);
    }

    // WebUI sends a PAGEID arg to identify the websocket it is using
    uint32_t WebUI_Server::getPageid(AsyncWebServerRequest* request) {
        if (request->hasParam("PAGEID")) {
            return request->getParam("PAGEID")->value().toInt();
        }
        return 0;  // ID 0 means none
    }

    void WebUI_Server::synchronousCommand(
        AsyncWebServerRequest* request, const char* cmd, bool silent, AuthenticationLevel auth_level, bool allowedInMotion) {
        // Can we do this with async?
        if (http_block_during_motion->get() && inMotionState() && !allowedInMotion) {  // ESP800 is to allow a cached paged reload on webui3
            request->send(503, "text/plain", "Try again when not moving\n");
            return;
        }
        char line[256];
        strncpy(line, cmd, 255);
        line[255] = '\0';
        if (request->method() == HTTP_GET) {
            const bool heavy = WebUI::ResourcePolicy::is_heavy_http_command(cmd);
            HeavyHttpReservation reservation(heavy);
            if (!reservation) {
                try {
                    request->send(503, "text/plain", "Web response resources busy\n");
                } catch (const std::bad_alloc&) {
                    abort_request_client(request);
                }
                return;
            }

            try {
                std::unique_ptr<WebClient> webClient(
                    new WebClient(heavy ? release_heavy_http_response : nullptr));
                if (heavy) {
                    reservation.transfer();
                }
                webClient->attachWS(silent);
                auto* clientOwner = webClient.get();
                std::unique_ptr<AsyncWebServerResponse> response(request->beginChunkedResponse(
                    "", [clientOwner, request](uint8_t* buffer, size_t maxLen, size_t total) mutable -> size_t {
                        return clientOwner->copyBufferSafe(buffer, min((int)maxLen, 1024), total);
                    }));
                if (!response) {
                    abort_request_client(request);
                    return;
                }
                response->addHeader(T_Cache_Control, T_no_cache);

                // Publish the WebClient to the disconnect/reap path only after
                // the response and callback are fully constructed.  Before
                // this point local unique_ptr ownership is exception-safe.
                request->onDisconnect([clientOwner]() {
                    clientOwner->detachWS();
                    if (!allChannels.kill(clientOwner) && !schedule_deferred_webclient_kill(clientOwner)) {
                        // WebClient is never registered in allChannels and
                        // detachWS() has joined its only background owner, so
                        // direct destruction is the bounded last-resort path.
                        delete clientOwner;
                    }
                });
                webClient.release();

                if (!clientOwner->executeCommandBackground(line)) {
                    clientOwner->cancelPendingCommand();
                    abort_request_client(request);
                    return;
                }

                auto* rawResponse = response.get();
                request->send(rawResponse);
                if (request->getResponse() == rawResponse) {
                    response.release();
                } else {
                    abort_request_client(request);
                }
            } catch (const std::bad_alloc&) {
                abort_request_client(request);
            }
            return;
        }

        try {
            std::unique_ptr<AsyncWebServerResponse> response(request->beginResponse(200, "", ""));
            if (!response) {
                abort_request_client(request);
                return;
            }
            response->addHeader(T_Cache_Control, T_no_cache);
            auto* rawResponse = response.get();
            request->send(rawResponse);
            if (request->getResponse() == rawResponse) {
                response.release();
            } else {
                abort_request_client(request);
            }
        } catch (const std::bad_alloc&) {
            abort_request_client(request);
        }
    }

    std::string getSession(AsyncClient* client) {
        if (!client) {
            return "";
        }
        return (std::to_string((uint32_t)IPAddress(client->getRemoteAddress())) + ":" + std::to_string(client->getRemotePort()));
    }
    std::string getSession(AsyncWebSocketClient* client) {
        if (!client) {
            return "";
        }
        return (std::to_string((uint32_t)client->remoteIP()) + ":" + std::to_string(client->remotePort()));
    }
    void WebUI_Server::websocketCommand(AsyncWebServerRequest* request, const char* cmd, uint32_t pageid, AuthenticationLevel auth_level) {
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            request->send(401, "text/plain", "Authentication failed\n");
            return;
        }
        std::string session = getSessionCookie(request);
        bool hasError = WSChannels::runGCode(pageid, cmd, session);
        request->send(hasError ? 500 : 200, "text/plain", hasError ? "WebSocket dead" : "");
    }

    bool WebUI_Server::isAllowedInMotion(String cmd) {
        if (cmd.startsWith("[ESP800]"))
            return true;

        return false;
    }
    void WebUI_Server::_handle_web_command(AsyncWebServerRequest* request, bool silent) {
        AuthenticationLevel auth_level = is_authenticated();
        if (request->hasParam("cmd") || request->hasParam("commandText")) {
            String cmd;
            if (request->hasParam("cmd"))
                cmd = request->getParam("cmd")->value();
            else
                cmd = request->getParam("commandText")->value();
            // [ESPXXX] commands expect data in the HTTP response
            String cmdUpper = cmd;
            cmdUpper.toUpperCase();
            if (cmdUpper.startsWith("[ESP") || cmdUpper.startsWith("$/")) {
                synchronousCommand(request, cmd.c_str(), silent, auth_level, isAllowedInMotion(cmdUpper));
            } else {
                websocketCommand(request, cmd.c_str(), getPageid(request), auth_level);
            }
            return;
        }
        if (request->hasParam("plain")) {
            synchronousCommand(request, request->getParam("plain")->value().c_str(), silent, auth_level);
            return;
        }
        request->send(500, "text/plain", "Invalid command");
    }

    //login status check
    void WebUI_Server::handle_login(AsyncWebServerRequest* request) {
#ifdef ENABLE_AUTHENTICATION
        const char* smsg;
        std::string sUser, sPassword;
        const char* auths;
        uint16_t    code            = 200;
        bool        msg_alert_error = false;
        //disconnect can be done anytime no need to check credential
        if (_webserver->hasArg("DISCONNECT")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            std::string sessionID;
            if (pos != std::string::npos) {
                size_t pos2 = cookie.find(";", pos);
                sessionID   = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
            }
            ClearAuthIP(_webserver->client().remoteIP(), sessionID);
            _webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0");
            _webserver->sendHeader(T_Cache_Control, T_no_cache);
            sendAuth("Ok", "guest", "");
            //_webserver->client().stop();
            return;
        }

        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            auths = "guest";
        } else if (auth_level == AuthenticationLevel::LEVEL_USER) {
            auths = "user";
        } else if (auth_level == AuthenticationLevel::LEVEL_ADMIN) {
            auths = "admin";
        } else {
            auths = "???";
        }

        //check is it is a submission or a query
        if (_webserver->hasArg("SUBMIT")) {
            //is there a correct list of query?
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER")) {
                //USER
                sUser = _webserver->arg("USER").c_str();
                if (!((sUser == DEFAULT_ADMIN_LOGIN) || (sUser == DEFAULT_USER_LOGIN))) {
                    msg_alert_error = true;
                    smsg            = "Error : Incorrect User";
                    code            = 401;
                }

                if (msg_alert_error == false) {
                    //Password
                    sPassword = _webserver->arg("PASSWORD").c_str();
                    std::string sadminPassword(admin_password->get());
                    std::string suserPassword(user_password->get());

                    if (!(sUser == DEFAULT_ADMIN_LOGIN && sPassword == sadminPassword) ||
                        (sUser == DEFAULT_USER_LOGIN && sPassword == suserPassword)) {
                        msg_alert_error = true;
                        smsg            = "Error: Incorrect password";
                        code            = 401;
                    }
                }
            } else {
                msg_alert_error = true;
                smsg            = "Error: Missing data";
                code            = 500;
            }
            //change password
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER") && _webserver->hasArg("NEWPASSWORD") &&
                (msg_alert_error == false)) {
                std::string newpassword(_webserver->arg("NEWPASSWORD").c_str());

                char pwdbuf[MAX_LOCAL_PASSWORD_LENGTH + 1];
                newpassword.toCharArray(pwdbuf, MAX_LOCAL_PASSWORD_LENGTH + 1);

                Error err;

                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    err = admin_password->setStringValue(pwdbuf);
                } else {
                    err = user_password->setStringValue(pwdbuf);
                }
                if (err != Error::Ok) {
                    msg_alert_error = true;
                    smsg            = "Error: Password cannot contain spaces";
                    code            = 500;
                }
            }
            if ((code == 200) || (code == 500)) {
                AuthenticationLevel current_auth_level;
                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_ADMIN;
                } else if (sUser == DEFAULT_USER_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_USER;
                } else {
                    current_auth_level = AuthenticationLevel::LEVEL_GUEST;
                }
                //create Session
                if ((current_auth_level != auth_level) || (auth_level == AuthenticationLevel::LEVEL_GUEST)) {
                    AuthenticationIP* current_auth = new AuthenticationIP;
                    current_auth->level            = current_auth_level;
                    current_auth->ip               = _webserver->client().remoteIP();
                    strcpy(current_auth->sessionID, create_session_ID());
                    strcpy(current_auth->userID, sUser.c_str());
                    current_auth->last_time = millis();
                    if (AddAuthIP(current_auth)) {
                        std::string tmps = "ESPSESSIONID=";
                        tmps += current_auth->sessionID.c_str();
                        _webserver->sendHeader("Set-Cookie", tmps);
                        _webserver->sendHeader(T_Cache_Control, T_no_cache);
                        switch (current_auth->level) {
                            case AuthenticationLevel::LEVEL_ADMIN:
                                auths = "admin";
                                break;
                            case AuthenticationLevel::LEVEL_USER:
                                auths = "user";
                                break;
                            default:
                                auths = "guest";
                                break;
                        }
                    } else {
                        delete current_auth;
                        msg_alert_error = true;
                        code            = 500;
                        smsg            = "Error: Too many connections";
                    }
                }
            }
            if (code == 200) {
                smsg = "Ok";
            }

            sendAuth("Ok", "guest", "");
        } else {
            if (auth_level != AuthenticationLevel::LEVEL_GUEST) {
                std::string cookie(_webserver->header("Cookie").c_str());
                size_t      pos = cookie.find("ESPSESSIONID=");
                std::string sessionID;
                if (pos != std::string::npos) {
                    size_t pos2                         = cookie.find(";", pos);
                    sessionID                           = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                    AuthenticationIP* current_auth_info = GetAuth(_webserver->client().remoteIP(), sessionID.c_str());
                    if (current_auth_info != NULL) {
                        sUser = current_auth_info->userID;
                    }
                }
            }
            sendAuth(smsg, auths, "");
        }
#else
        sendAuth(request, "Ok", "admin", "");
#endif
    }

    // This page is used when you try to reload WebUI during motion,
    // to avoid interrupting that motion.  It lets you wait until
    // motion is finished.
    void WebUI_Server::handleReloadBlocked(AsyncWebServerRequest* request) {
        request->send(503,
                      "text/html",
                      "<!DOCTYPE html><html><body>"
                      "<h3>Cannot load WebUI while GCode Program is Running</h3>"

                      "<button onclick='window.location.replace(\"/feedhold_reload\")'>Pause</button>"
                      "&nbsp;Pause the GCode program with feedhold<br><br>"

                      "<button onclick='window.location.replace(\"/restart_reload\")'>Stop</button>"
                      "&nbsp;Stop the GCode Program with reset<br><br>"

                      "<button onclick='window.location.reload()'>Reload WebUI</button>"
                      "&nbsp;(You must first stop the GCode program or wait for it to finish)<br><br>"

                      "</body></html>");
    }
    void WebUI_Server::handleDidRestart(AsyncWebServerRequest* request) {
        request->send(503,
                      "text/html",
                      "<!DOCTYPE html><html><body>"
                      "<h3>GCode Program has been stopped</h3>"
                      "<button onclick='window.location.replace(\"/\")'>Reload WebUI</button>"
                      "</body></html>");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleFeedholdReload(AsyncWebServerRequest* request) {
        protocol_send_event(&feedHoldEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleCyclestartReload(AsyncWebServerRequest* request) {
        protocol_send_event(&cycleStartEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleRestartReload(AsyncWebServerRequest* request) {
        protocol_send_event(&rtResetEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/did_restart");
    }

    // push error code and message to websocket.  Used by upload code
    void WebUI_Server::pushError(AsyncWebServerRequest* request, uint16_t code, const char* st, int32_t web_error, uint16_t timeout) {
        if (_socket_server && st) {
            std::string s("ERROR:");
            s += std::to_string(code) + ":";
            s += st;

            WSChannels::sendError(getPageid(request), st, getSessionCookie(request));

            if (web_error != 0 && request) {
                request->send(web_error, "text/xml", st);
            }
        }
    }

    //abort reception of packages
    void WebUI_Server::cancelUpload(AsyncWebServerRequest* request) {
        request->client()->close();
        delay(100);
    }

    //LocalFS files uploader handle
    void WebUI_Server::fileUpload(
        AsyncWebServerRequest* request, const Volume& fs, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (!index) {
            std::string sizeargname(filename.c_str());
            sizeargname += "S";
            size_t filesize = request->hasParam(sizeargname.c_str()) ? request->getParam(sizeargname.c_str())->value().toInt() : 0;
            uploadStart(request, filename.c_str(), filesize, fs);
        }
        if (_upload_status == UploadStatus::ONGOING) {
            uploadWrite(request, data, len);
            if (final) {
                std::string sizeargname(filename.c_str());
                sizeargname += "S";
                size_t filesize = request->hasParam(sizeargname.c_str()) ? request->getParam(sizeargname.c_str())->value().toInt() : 0;
                uploadEnd(request, filesize);
            }
        } else {
            uploadStop();
            return;
        }

        uploadCheck(request);

        return;
    }

    void WebUI_Server::sendJSON(AsyncWebServerRequest* request, uint16_t code, const char* s) {
        AsyncWebServerResponse* response = request->beginResponse(code, T_application_json, s);
        response->addHeader(T_Cache_Control, T_no_cache);
        request->send(response);
    }

    void WebUI_Server::sendAuth(AsyncWebServerRequest* request, const char* status, const char* level, const char* user) {
        AsyncResponseStream* response = request->beginResponseStream(T_application_json);
        response->setCode(200);
        response->addHeader(T_Cache_Control, T_no_cache);

        JSONencoder j([response](const char* s) { response->print(s); });
        j.begin();
        j.member("status", status);
        if (*level != '\0') {
            j.member("authentication_lvl", level);
        }
        if (*user != '\0') {
            j.member("user", user);
        }
        j.end();
        request->send(response);
    }

    void WebUI_Server::sendStatus(AsyncWebServerRequest* request, uint16_t code, const char* status) {
        AsyncResponseStream* response = request->beginResponseStream(T_application_json);
        response->setCode(code);
        response->addHeader(T_Cache_Control, T_no_cache);

        JSONencoder j([response](const char* s) { response->print(s); });
        j.begin();
        j.member("status", status);
        j.end();
        request->send(response);
    }

    void WebUI_Server::sendAuthFailed(AsyncWebServerRequest* request) {
        sendStatus(request, 401, "Authentication failed");
    }

    void WebUI_Server::LocalFSFileupload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        fileUpload(request, LocalFS, filename, index, data, len, final);
    }
    void WebUI_Server::SDFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        fileUpload(request, SD, filename, index, data, len, final);
    }

    //Web Update handler
    void WebUI_Server::handleUpdate(AsyncWebServerRequest* request) {
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level != AuthenticationLevel::LEVEL_ADMIN) {
            request->send(403, "text/plain", "Not allowed, log in first!\n");
            return;
        }

        //if success restart
        if (_upload_status == UploadStatus::SUCCESSFUL) {
            sendStatus(request, 200, std::to_string(int(_upload_status)).c_str());
            _schedule_reboot_time = millis() + 1000;
            _schedule_reboot      = true;
        } else {
            sendStatus(request, 200, std::to_string(int(_upload_status)).c_str());
            if (_upload_status != UploadStatus::ONGOING) {
                _upload_status = UploadStatus::NONE;
            }
        }
    }

#ifdef HAVE_UPDATE
    //File upload for Web update
    void WebUI_Server::WebUpdateUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        constexpr uint32_t firmwareUploadRxTimeoutSeconds = 30;
        static size_t   last_upload_update;
        uint32_t maxSketchSpace = firmware_ota_expected_bytes.load(std::memory_order_relaxed);

        //only admin can update FW
        if (is_authenticated() != AuthenticationLevel::LEVEL_ADMIN) {
            if (_firmware_upload_request == request) {
                _upload_status = UploadStatus::FAILED;
            }
            log_info("Upload rejected");
            sendAuthFailed(request);
            request->abort();
            return;
            //pushError(request, ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
        } else {
            //Upload start
            //**************
            if (!index) {  //upload.status == UPLOAD_FILE_START) {
                if (_firmware_upload_request != nullptr && _firmware_upload_request != request) {
                    log_warn("Concurrent firmware upload rejected");
                    firmware_ota_failures.fetch_add(1, std::memory_order_relaxed);
                    request->abort();
                    return;
                }
                if (Update.isRunning()) {
                    log_warn("Firmware upload rejected because an updater is already active");
                    firmware_ota_failures.fetch_add(1, std::memory_order_relaxed);
                    request->abort();
                    return;
                }

                try {
                    request->onDisconnect([request]() {
                        if (_firmware_upload_request == request) {
                            if (firmware_ota_update_started.load(std::memory_order_relaxed) != 0 && Update.isRunning()) {
                                Update.abort();
                                firmware_ota_disconnect_aborts.fetch_add(1, std::memory_order_relaxed);
                                record_firmware_ota_failure();
                            }
                            firmware_ota_update_started.store(0, std::memory_order_relaxed);
                            if (_upload_status == UploadStatus::ONGOING) {
                                _upload_status = UploadStatus::FAILED;
                            }
                            _firmware_upload_request = nullptr;
                            firmware_ota_active.store(0, std::memory_order_relaxed);
                        }
                    });
                } catch (const std::bad_alloc&) {
                    firmware_ota_failures.fetch_add(1, std::memory_order_relaxed);
                    request->abort();
                    return;
                }

                _firmware_upload_request = request;
                firmware_ota_active.store(1, std::memory_order_relaxed);
                firmware_ota_expected_bytes.store(0, std::memory_order_relaxed);
                firmware_ota_accepted_bytes.store(0, std::memory_order_relaxed);
                firmware_ota_max_write_us.store(0, std::memory_order_relaxed);
                firmware_ota_failure_recorded.store(0, std::memory_order_relaxed);
                firmware_ota_update_started.store(0, std::memory_order_relaxed);
                request->client()->setRxTimeout(firmwareUploadRxTimeoutSeconds);

                log_info("Update Firmware");
                _upload_status = UploadStatus::ONGOING;
                std::string sizeargname(filename.c_str());
                sizeargname += "S";
                if (request->hasParam(sizeargname.c_str(), true)) {
                    const int32_t requestedSize = request->getParam(sizeargname.c_str(), true)->value().toInt();
                    if (requestedSize > 0) {
                        maxSketchSpace = static_cast<uint32_t>(requestedSize);
                        firmware_ota_expected_bytes.store(maxSketchSpace, std::memory_order_relaxed);
                    } else {
                        _upload_status = UploadStatus::FAILED;
                        record_firmware_ota_failure();
                        pushError(request, ESP_ERROR_UPLOAD, "Upload rejected, invalid firmware size");
                    }
                } else {
                    _upload_status = UploadStatus::FAILED;
                    record_firmware_ota_failure();
                    pushError(request, ESP_ERROR_UPLOAD, "Upload rejected, missing firmware size");
                }
                //check space
                size_t flashsize = 0;
                if (esp_ota_get_running_partition()) {
                    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                    if (partition) {
                        flashsize = partition->size;
                    }
                }
                if (flashsize < maxSketchSpace) {
                    String msg = String("Upload rejected, not enough space (needs " + String(maxSketchSpace) + ", has " + String(flashsize));
                    pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, msg.c_str());
                    _upload_status = UploadStatus::FAILED;
                    record_firmware_ota_failure();
                    log_info("Update cancelled");
                }
                if (_upload_status != UploadStatus::FAILED) {
                    last_upload_update = 0;
                    if (!Update.begin(maxSketchSpace, U_FLASH)) {
                        _upload_status = UploadStatus::FAILED;
                        record_firmware_ota_failure();
                        log_info("Update cancelled");
                        pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                    } else {
                        firmware_ota_update_started.store(1, std::memory_order_relaxed);
                        log_info("Update 0%");
                    }
                }
            }
            //Upload write
            //**************
            //check if no error
            if (_upload_status == UploadStatus::ONGOING) {
                if (((100 * index) / maxSketchSpace) != last_upload_update) {
                    if (maxSketchSpace > 0) {
                        last_upload_update = (100 * index) / maxSketchSpace;
                    } else {
                        last_upload_update = index;
                    }

                    log_info("Update " << last_upload_update << "%");
                }
                const int64_t writeStartedUs = esp_timer_get_time();
                const size_t written = Update.write(data, len);
                const uint64_t writeElapsedUs = static_cast<uint64_t>(esp_timer_get_time() - writeStartedUs);
                update_max(firmware_ota_max_write_us,
                           static_cast<uint32_t>(std::min<uint64_t>(writeElapsedUs, std::numeric_limits<uint32_t>::max())));
                if (written == len) {
                    firmware_ota_accepted_bytes.store(static_cast<uint32_t>(index + written), std::memory_order_relaxed);
                } else {
                    _upload_status = UploadStatus::FAILED;
                    record_firmware_ota_failure();
                    log_info("Update write failed");
                    pushError(request, ESP_ERROR_FILE_WRITE, "File write failed");
                }
            }
            //Upload end
            //**************
            if (final) {
                if (_upload_status == UploadStatus::ONGOING && index + len == maxSketchSpace && Update.end(false)) {
                    firmware_ota_update_started.store(0, std::memory_order_relaxed);
                    //Now Reboot
                    log_info("Update 100%");
                    _upload_status = UploadStatus::SUCCESSFUL;
                } else {
                    if (Update.isRunning()) {
                        Update.abort();
                    }
                    firmware_ota_update_started.store(0, std::memory_order_relaxed);
                    _upload_status = UploadStatus::FAILED;
                    record_firmware_ota_failure();
                    log_info("Update failed");
                    pushError(request, ESP_ERROR_UPLOAD, "Update upload failed");
                }
            }
        }
    }
#endif

    void WebUI_Server::handleFileOps(AsyncWebServerRequest* request, const Volume& fs) {
        //this is only for admin and user
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _upload_status = UploadStatus::NONE;
            sendAuthFailed(request);
            return;
        }

        std::error_code ec;

        std::string path("");
        std::string sstatus("Ok");
        if ((_upload_status == UploadStatus::FAILED) || (_upload_status == UploadStatus::FAILED)) {
            sstatus = "Upload failed";
        }
        _upload_status      = UploadStatus::NONE;
        bool     list_files = true;

        //get current path
        if (request->hasParam("path")) {
            path += request->getParam("path")->value().c_str();
        } else if (!_uploadPath.empty()) {
            // If no path parameter but we have a stored upload path, use it
            path = _uploadPath;
            _uploadPath.clear();  // Clear it after use
        }

        if (!path.empty()) {
            // path.trim();
            replace_string_in_place(path, "//", "/");
            if (path[path.length() - 1] == '/') {
                path = path.substr(0, path.length() - 1);
            }
            if (path.length() && path[0] == '/') {
                path = path.substr(1);
            }
        }

        FluidPath fpath { path, fs, ec };
        if (ec) {
            sendJSON(request, 200, "{\"status\":\"No SD card\"}");
            return;
        }

        // Handle deletions and directory creation
        if (request->hasParam("action") && request->hasParam("filename")) {
            std::string action(request->getParam("action")->value().c_str());
            std::string filename = std::string(request->getParam("filename")->value().c_str());
            if (action == "delete") {
                if (stdfs::remove(fpath / filename, ec)) {
                    sstatus = filename + " deleted";
                    HashFS::delete_file(fpath / filename);
                } else {
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "deletedir") {
                stdfs::path dirpath { fpath / filename };
                log_debug("Deleting directory " << dirpath.string().c_str());
                size_t count = stdfs::remove_all(dirpath, ec);
                if (count > 0) {
                    sstatus = filename + " deleted";
                    HashFS::report_change();
                } else {
                    log_debug("remove_all returned " << count);
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "createdir") {
                if (stdfs::create_directory(fpath / filename, ec)) {
                    sstatus = filename + " created";
                    HashFS::report_change();
                } else {
                    sstatus = "Cannot create ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "rename") {
                if (!request->hasParam("newname")) {
                    sstatus = "Missing new filename";
                } else {
                    std::string newname = std::string(request->getParam("newname")->value().c_str());
                    std::filesystem::rename(fpath / filename, fpath / newname, ec);
                    if (ec) {
                        sstatus = "Cannot rename ";
                        sstatus += filename + " " + ec.message();
                    } else {
                        sstatus = filename + " renamed to " + newname;
                        HashFS::rename_file(fpath / filename, fpath / newname);
                    }
                }
            }
        }

        //check if no need build file list
        if (request->hasParam("dontlist") && request->getParam("dontlist")->value() == "yes") {
            list_files = false;
        }

        request->send(create_file_list_response(request, fpath, path, sstatus, list_files));
    }

    void WebUI_Server::handle_direct_SDFileList(AsyncWebServerRequest* request) {
        handleFileOps(request, SD);
    }
    void WebUI_Server::handleFileList(AsyncWebServerRequest* request) {
        handleFileOps(request, LocalFS);
    }

    // File upload
    void WebUI_Server::uploadStart(AsyncWebServerRequest* request, const char* filename, size_t filesize, const Volume& fs) {
        std::error_code ec;

        FluidPath fpath { filename, fs, ec };
        if (ec) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload filesystem inaccessible");
            pushError(request, ESP_ERROR_FILE_CREATION, "Upload rejected, filesystem inaccessible");
            return;
        }

        // Store the directory path of the uploaded file for later listing
        stdfs::path filepath(filename);
        _uploadPath = filepath.parent_path().string();
        if (_uploadPath == ".") {
            _uploadPath = "";  // Root directory
        }

        auto space = stdfs::space(fpath, ec);
        if (!ec && filesize && filesize > space.available) {
            // If the file already exists, maybe there will be enough space
            // when we replace it.
            auto existing_size = stdfs::file_size(fpath, ec);
            if (ec || (filesize > (space.available + existing_size))) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload not enough space");
                pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                return;
            }
        }

        if (_upload_status != UploadStatus::FAILED) {
            //Create file for writing
            try {
                _uploadFile    = new FileStream(fpath, "w");
                _upload_status = UploadStatus::ONGOING;
            } catch (const ErrorException& err) {
                _uploadFile    = nullptr;
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - cannot create file");
                pushError(request, ESP_ERROR_FILE_CREATION, "File creation failed");
            }
        }
    }

    void WebUI_Server::uploadWrite(AsyncWebServerRequest* request, uint8_t* buffer, size_t length) {
        delay_ms(1);
        if (_uploadFile && _upload_status == UploadStatus::ONGOING) {
            //no error write post data
            if (length != _uploadFile->write(buffer, length)) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - file write failed");
                pushError(request, ESP_ERROR_FILE_WRITE, "File write failed");
            }
        } else {  //if error set flag UploadStatus::FAILED
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(request, ESP_ERROR_FILE_WRITE, "File not open");
        }
    }

    void WebUI_Server::uploadEnd(AsyncWebServerRequest* request, size_t filesize) {
        //if file is open close it
        if (_uploadFile) {
            //            delete _uploadFile;
            // _uploadFile = nullptr;

            std::string pathname = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            log_debug("pathname " << pathname);

            FluidPath filepath { pathname, LocalFS };

            HashFS::rehash_file(filepath);

            // Check size
            if (filesize) {
                size_t actual_size;
                try {
                    actual_size = stdfs::file_size(filepath);
                } catch (const ErrorException& err) { actual_size = 0; }

                if (filesize != actual_size) {
                    _upload_status = UploadStatus::FAILED;
                    pushError(request, ESP_ERROR_UPLOAD, "File upload mismatch");
                    log_info("Upload failed - size mismatch - exp " << filesize << " got " << actual_size);
                }
            }
        } else {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(request, ESP_ERROR_FILE_CLOSE, "File close failed");
        }
        if (_upload_status == UploadStatus::ONGOING) {
            _upload_status = UploadStatus::SUCCESSFUL;
        } else {
            _upload_status = UploadStatus::FAILED;
            pushError(request, ESP_ERROR_UPLOAD, "Upload error 8");
        }
    }
    void WebUI_Server::uploadStop() {
        _upload_status = UploadStatus::FAILED;
        _uploadPath.clear();  // Clear stored upload path on failure
        if (_uploadFile) {
            log_info("Upload cancelled");
            std::filesystem::path filepath = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            HashFS::rehash_file(filepath);
        }
    }
    void WebUI_Server::uploadCheck(AsyncWebServerRequest* request) {
        std::error_code error_code;
        if (_upload_status == UploadStatus::FAILED) {
            cancelUpload(request);
            if (_uploadFile) {
                std::filesystem::path filepath = _uploadFile->fpath();
                delete _uploadFile;
                _uploadFile = nullptr;
                stdfs::remove(filepath, error_code);
                HashFS::rehash_file(filepath);
            }
        }
    }

    void WebUI_Server::poll() {
        static uint32_t cleanup_time = millis();
        static uint32_t ping_time    = millis();
#ifdef HAVE_DNS
        if (WiFi.getMode() == WIFI_AP) {
            dnsServer.processNextRequest();
        }
#endif
        expire_pending_websocket_slots();
        process_deferred_websocket_closes(_socket_server);
        process_deferred_webclient_kills();
        if (_schedule_reboot and _schedule_reboot_time == millis()) {
            _schedule_reboot = false;
            protocol_send_event(&fullResetEvent);
        }
        if ((millis() - cleanup_time) > 1000) {
            if (_socket_server) {
                _socket_server->cleanupClients(ResourcePolicy::max_websocket_clients);
            }
            cleanup_time = millis();
        }
        if ((millis() - ping_time) > 10000) {
            uint32_t heapsize = xPortGetFreeHeapSize();
            log_verbose("memory: " << heapsize << " min: " << heapLowWater);
            if (_socket_server) {
                WSChannels::sendPing();
            }
            ping_time = millis();
        }
    }

    //check authentication
    AuthenticationLevel WebUI_Server::is_authenticated() {
#ifdef ENABLE_AUTHENTICATION
        if (_webserver->hasHeader("Cookie")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            if (pos != std::string::npos) {
                size_t      pos2      = cookie.find(";", pos);
                std::string sessionID = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                IPAddress   ip        = _webserver->client().remoteIP();
                //check if cookie can be reset and clean table in same time
                return ResetAuthIP(ip, sessionID.c_str());
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
#else
        return AuthenticationLevel::LEVEL_ADMIN;
#endif
    }

#ifdef ENABLE_AUTHENTICATION

    //add the information in the linked list if possible
    bool WebUI_Server::AddAuthIP(AuthenticationIP* item) {
        if (_nb_ip > MAX_AUTH_IP) {
            return false;
        }
        item->_next = _head;
        _head       = item;
        _nb_ip++;
        return true;
    }

    //Session ID based on IP and time using 16 char
    const char* WebUI_Server::create_session_ID() {
        static char sessionID[17];
        //reset SESSIONID
        for (size_t i = 0; i < 17; i++) {
            sessionID[i] = '\0';
        }
        //get time
        uint32_t now = millis();
        //get remote IP
        IPAddress remoteIP = _webserver->client().remoteIP();
        //generate SESSIONID
        if (0 > snprintf(sessionID,
                         17,
                         "%02X%02X%02X%02X%02X%02X%02X%02X",
                         remoteIP[0],
                         remoteIP[1],
                         remoteIP[2],
                         remoteIP[3],
                         (uint8_t)((now >> 0) & 0xff),
                         (uint8_t)((now >> 8) & 0xff),
                         (uint8_t)((now >> 16) & 0xff),
                         (uint8_t)((now >> 24) & 0xff))) {
            strcpy(sessionID, "NONE");
        }
        return sessionID;
    }

    bool WebUI_Server::ClearAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        bool              done     = false;
        while (current) {
            if ((ip == current->ip) && (strcmp(sessionID, current->sessionID) == 0)) {
                //remove
                done = true;
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                previous = current;
                current  = current->_next;
            }
        }
        return done;
    }

    //Get info
    AuthenticationIP* WebUI_Server::GetAuth(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current = _head;
        //AuthenticationIP * previous = NULL;
        while (current) {
            if (ip == current->ip) {
                if (strcmp(sessionID, current->sessionID) == 0) {
                    //found
                    return current;
                }
            }
            //previous = current;
            current = current->_next;
        }
        return NULL;
    }

    //Review all IP to reset timers
    AuthenticationLevel WebUI_Server::ResetAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        while (current) {
            if ((millis() - current->last_time) > 360000) {
                //remove
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                if (ip == current->ip && strcmp(sessionID, current->sessionID) == 0) {
                    //reset time
                    current->last_time = millis();
                    return (AuthenticationLevel)current->level;
                }
                previous = current;
                current  = current->_next;
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
    }
#endif

    ModuleFactory::InstanceBuilder<WebUI_Server> __attribute__((init_priority(108))) webui_server_module("webuiserver", true);
}
