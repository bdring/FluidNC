// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace WebUI {
    namespace OutputUrlHttp {

        constexpr size_t MaxUrlLength       = 255;
        constexpr size_t MaxHostLength      = 253;
        constexpr size_t MaxTargetLength    = 255;
        constexpr size_t MaxAuthorityLength = MaxHostLength + 6;
        constexpr size_t MaxRequestLength   = 640;
        constexpr size_t MaxStatusLine      = 192;
        constexpr size_t MaxHeaderLine      = 512;

        struct ParsedUrl {
            bool     secure = false;
            uint16_t port   = 0;
            char     host[MaxHostLength + 1] {};
            char     authority[MaxAuthorityLength + 1] {};
            char     target[MaxTargetLength + 1] {};
        };

        inline bool copy_range(char* destination, size_t capacity, const char* begin, size_t length) {
            if (!destination || !capacity || !begin || length >= capacity) {
                return false;
            }
            std::memcpy(destination, begin, length);
            destination[length] = '\0';
            return true;
        }

        inline bool clean_url_character(unsigned char value) {
            // Static URLs must use percent encoding for whitespace/control bytes.
            // This also keeps request-line and Host-header construction injection-safe.
            return value > 0x20 && value != 0x7f;
        }

        inline bool valid_host_character(unsigned char value) {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                   (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_';
        }

        inline bool parse_url(const char* url, ParsedUrl& result) {
            result = {};
            if (!url) {
                return false;
            }

            size_t length = 0;
            while (length <= MaxUrlLength && url[length]) {
                if (!clean_url_character(static_cast<unsigned char>(url[length])) || url[length] == '#') {
                    return false;
                }
                ++length;
            }
            if (length == 0 || length > MaxUrlLength || url[length] != '\0') {
                return false;
            }

            size_t scheme_length = 0;
            if (length >= 7 && std::memcmp(url, "http://", 7) == 0) {
                result.secure = false;
                result.port   = 80;
                scheme_length = 7;
            } else if (length >= 8 && std::memcmp(url, "https://", 8) == 0) {
                result.secure = true;
                result.port   = 443;
                scheme_length = 8;
            } else {
                return false;
            }

            const size_t authority_start = scheme_length;
            size_t       authority_end   = authority_start;
            while (authority_end < length && url[authority_end] != '/' && url[authority_end] != '?') {
                if (url[authority_end] == '@' || url[authority_end] == '[' || url[authority_end] == ']') {
                    return false;
                }
                ++authority_end;
            }
            if (authority_end == authority_start) {
                return false;
            }

            size_t colon = authority_end;
            for (size_t index = authority_start; index < authority_end; ++index) {
                if (url[index] == ':') {
                    if (colon != authority_end) {
                        return false;  // IPv6 literals are not supported by this IPv4 transport.
                    }
                    colon = index;
                }
            }

            const size_t host_end    = colon == authority_end ? authority_end : colon;
            const size_t host_length = host_end - authority_start;
            if (host_length == 0 || host_length > MaxHostLength) {
                return false;
            }
            for (size_t index = authority_start; index < host_end; ++index) {
                if (!valid_host_character(static_cast<unsigned char>(url[index]))) {
                    return false;
                }
            }
            if (!copy_range(result.host, sizeof(result.host), url + authority_start, host_length)) {
                return false;
            }

            bool explicit_port = false;
            if (colon != authority_end) {
                explicit_port = true;
                if (colon + 1 == authority_end) {
                    return false;
                }
                uint32_t port = 0;
                for (size_t index = colon + 1; index < authority_end; ++index) {
                    const unsigned char value = static_cast<unsigned char>(url[index]);
                    if (value < '0' || value > '9') {
                        return false;
                    }
                    port = port * 10u + static_cast<uint32_t>(value - '0');
                    if (port > 65535u) {
                        return false;
                    }
                }
                if (port == 0) {
                    return false;
                }
                result.port = static_cast<uint16_t>(port);
            }

            int authority_count = 0;
            if (explicit_port) {
                authority_count = std::snprintf(result.authority, sizeof(result.authority), "%s:%u", result.host,
                                                static_cast<unsigned>(result.port));
            } else {
                authority_count = std::snprintf(result.authority, sizeof(result.authority), "%s", result.host);
            }
            if (authority_count < 0 || static_cast<size_t>(authority_count) >= sizeof(result.authority)) {
                return false;
            }

            if (authority_end == length) {
                result.target[0] = '/';
                result.target[1] = '\0';
                return true;
            }

            const bool   query_only   = url[authority_end] == '?';
            const size_t source_size  = length - authority_end;
            const size_t target_size  = source_size + (query_only ? 1u : 0u);
            if (target_size == 0 || target_size > MaxTargetLength) {
                return false;
            }
            size_t target_offset = 0;
            if (query_only) {
                result.target[target_offset++] = '/';
            }
            std::memcpy(result.target + target_offset, url + authority_end, source_size);
            result.target[target_size] = '\0';
            return true;
        }

        inline bool parse_status_line(const char* line, uint16_t& status) {
            status = 0;
            if (!line) {
                return false;
            }
            size_t length = 0;
            while (length < MaxStatusLine && line[length]) {
                ++length;
            }
            if (length < 12 || length == MaxStatusLine ||
                (std::memcmp(line, "HTTP/1.0 ", 9) != 0 && std::memcmp(line, "HTTP/1.1 ", 9) != 0)) {
                return false;
            }
            const unsigned char first  = static_cast<unsigned char>(line[9]);
            const unsigned char second = static_cast<unsigned char>(line[10]);
            const unsigned char third  = static_cast<unsigned char>(line[11]);
            if (first < '0' || first > '9' || second < '0' || second > '9' || third < '0' || third > '9') {
                return false;
            }
            if (line[12] != '\0' && line[12] != ' ') {
                return false;
            }
            const uint16_t value = static_cast<uint16_t>((first - '0') * 100 + (second - '0') * 10 + (third - '0'));
            if (value < 100 || value > 599) {
                return false;
            }
            status = value;
            return true;
        }

        inline uint32_t remaining_ms(uint32_t start_ms, uint32_t timeout_ms, uint32_t now_ms) {
            if (timeout_ms == 0 || timeout_ms >= 0x80000000u) {
                return 0;
            }
            const uint32_t elapsed = now_ms - start_ms;
            return elapsed >= timeout_ms ? 0u : timeout_ms - elapsed;
        }

        inline uint32_t secure_phase_timeout_seconds(uint32_t remaining) {
            // Arduino-ESP32 applies its TCP-connect timeout and TLS-handshake
            // timeout sequentially. Split the one caller budget so the two
            // independently bounded phases cannot add up beyond it.
            return remaining / 2000u;
        }

        template <typename Client>
        class StopGuard {
            Client& _client;

        public:
            explicit StopGuard(Client& client) : _client(client) {}
            StopGuard(const StopGuard&)            = delete;
            StopGuard& operator=(const StopGuard&) = delete;
            ~StopGuard() noexcept {
                try {
                    _client.stop();
                } catch (...) {
                    // The real WiFi clients have no-throw stop().  Keep the C ABI
                    // fail-closed even if a test/different adapter violates that contract.
                }
            }
        };

        template <typename Client>
        bool read_line(Client& client, uint32_t start_ms, uint32_t timeout_ms, char* line, size_t capacity) {
            if (!line || capacity < 2) {
                return false;
            }
            size_t used = 0;
            for (;;) {
                if (remaining_ms(start_ms, timeout_ms, client.now_ms()) == 0) {
                    return false;
                }
                if (client.available() > 0) {
                    const int value = client.read();
                    if (value < 0) {
                        return false;
                    }
                    if (value == '\n') {
                        if (used && line[used - 1] == '\r') {
                            --used;
                        }
                        line[used] = '\0';
                        return true;
                    }
                    if (used + 1 >= capacity) {
                        return false;
                    }
                    line[used++] = static_cast<char>(value);
                    continue;
                }
                if (!client.connected()) {
                    return false;
                }
                client.idle();
            }
        }

        template <typename Client>
        bool perform_get(Client& client, const ParsedUrl& parsed, uint32_t timeout_ms, uint16_t* http_status) {
            uint16_t local_status = 0;
            if (!http_status) {
                http_status = &local_status;
            }
            *http_status = 0;
            StopGuard<Client> stop_guard(client);

            const uint32_t start_ms = client.now_ms();
            uint32_t       remaining = remaining_ms(start_ms, timeout_ms, client.now_ms());
            if (remaining == 0 || !client.connect(parsed, remaining)) {
                return false;
            }
            remaining = remaining_ms(start_ms, timeout_ms, client.now_ms());
            if (remaining == 0) {
                return false;
            }

            char request[MaxRequestLength] {};
            const int request_size = std::snprintf(request, sizeof(request),
                                                   "GET %s HTTP/1.1\r\n"
                                                   "Host: %s\r\n"
                                                   "Connection: close\r\n"
                                                   "User-Agent: FluidNC-OutputUrl/1\r\n"
                                                   "Accept: */*\r\n\r\n",
                                                   parsed.target, parsed.authority);
            if (request_size <= 0 || static_cast<size_t>(request_size) >= sizeof(request)) {
                return false;
            }

            size_t written = 0;
            while (written < static_cast<size_t>(request_size)) {
                remaining = remaining_ms(start_ms, timeout_ms, client.now_ms());
                if (remaining == 0) {
                    return false;
                }
                const size_t count = client.write(reinterpret_cast<const uint8_t*>(request) + written,
                                                  static_cast<size_t>(request_size) - written, remaining);
                if (count > static_cast<size_t>(request_size) - written) {
                    return false;
                }
                if (count == 0) {
                    if (!client.connected()) {
                        return false;
                    }
                    client.idle();
                    continue;
                }
                written += count;
            }

            // RFC 9110 permits interim 1xx responses.  Consume a small bounded
            // number and report only the final response status.
            char line[MaxHeaderLine] {};
            for (unsigned interim_count = 0; interim_count < 4; ++interim_count) {
                if (!read_line(client, start_ms, timeout_ms, line, MaxStatusLine)) {
                    return false;
                }
                uint16_t status = 0;
                if (!parse_status_line(line, status)) {
                    return false;
                }
                if (status == 101 || status >= 200) {
                    *http_status = status;
                    return true;
                }
                do {
                    if (!read_line(client, start_ms, timeout_ms, line, sizeof(line))) {
                        return false;
                    }
                } while (line[0] != '\0');
            }
            return false;
        }

        template <typename Client>
        bool perform_get_noexcept(Client& client, const ParsedUrl& parsed, uint32_t timeout_ms, uint16_t* http_status) noexcept {
            if (http_status) {
                *http_status = 0;
            }
            try {
                return perform_get(client, parsed, timeout_ms, http_status);
            } catch (...) {
                if (http_status) {
                    *http_status = 0;
                }
                return false;
            }
        }

    }  // namespace OutputUrlHttp
}  // namespace WebUI
