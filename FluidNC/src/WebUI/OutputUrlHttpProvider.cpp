// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "OutputUrlHttpPolicy.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>

namespace WebUI {
    namespace OutputUrlHttp {
        namespace {
            constexpr size_t DnsSlotCount = 4;

            struct DnsSlot {
                char         host[MaxHostLength + 1] {};
                bool         in_flight = false;
                bool         done      = false;
                bool         success   = false;
                ip_addr_t    address {};
            };

            DnsSlot       dns_slots[DnsSlotCount] {};
            portMUX_TYPE  dns_mux = portMUX_INITIALIZER_UNLOCKED;
            std::atomic_flag provider_busy = ATOMIC_FLAG_INIT;

            class BusyGuard {
            public:
                ~BusyGuard() { provider_busy.clear(std::memory_order_release); }
            };

            void complete_dns_slot(DnsSlot& slot, const ip_addr_t* address) {
                portENTER_CRITICAL(&dns_mux);
                slot.in_flight = false;
                slot.done      = true;
                slot.success   = address && IP_IS_V4(address);
                if (slot.success) {
                    ip_addr_copy(slot.address, *address);
                } else {
                    ip_addr_set_zero(&slot.address);
                }
                portEXIT_CRITICAL(&dns_mux);
            }

            void dns_found(const char* name, const ip_addr_t* address, void* argument) {
                auto* slot = static_cast<DnsSlot*>(argument);
                if (!slot || !name) {
                    return;
                }

                bool matches = false;
                portENTER_CRITICAL(&dns_mux);
                matches = slot->in_flight && std::strncmp(slot->host, name, sizeof(slot->host)) == 0;
                portEXIT_CRITICAL(&dns_mux);
                if (matches) {
                    complete_dns_slot(*slot, address);
                }
            }

            bool copy_ipv4(const ip_addr_t& source, IPAddress& destination) {
                if (!IP_IS_V4(&source)) {
                    return false;
                }
                destination = ip4_addr_get_u32(ip_2_ip4(&source));
                return static_cast<uint32_t>(destination) != 0;
            }

            bool snapshot_dns_result(DnsSlot& slot, const char* host, IPAddress& destination, bool& pending) {
                ip_addr_t address {};
                bool      done    = false;
                bool      success = false;
                portENTER_CRITICAL(&dns_mux);
                if (std::strncmp(slot.host, host, sizeof(slot.host)) == 0) {
                    pending = slot.in_flight;
                    done    = slot.done;
                    success = slot.success;
                    if (success) {
                        ip_addr_copy(address, slot.address);
                    }
                } else {
                    pending = false;
                }
                portEXIT_CRITICAL(&dns_mux);
                return done && success && copy_ipv4(address, destination);
            }

            bool resolve_host(const char* host, uint32_t timeout_ms, IPAddress& destination) {
                if (!host || timeout_ms == 0 || timeout_ms >= 0x80000000u) {
                    return false;
                }
                if (destination.fromString(host)) {
                    return static_cast<uint32_t>(destination) != 0;
                }

                const uint32_t start_ms = millis();

                DnsSlot* slot        = nullptr;
                DnsSlot* recyclable  = nullptr;
                bool     start_query = false;
                portENTER_CRITICAL(&dns_mux);
                for (auto& candidate : dns_slots) {
                    if (candidate.host[0] && std::strncmp(candidate.host, host, sizeof(candidate.host)) == 0) {
                        slot = &candidate;
                        break;
                    }
                    if (!candidate.in_flight && !recyclable) {
                        recyclable = &candidate;
                    }
                }
                if (!slot) {
                    slot = recyclable;
                    if (slot) {
                        std::strncpy(slot->host, host, sizeof(slot->host) - 1);
                        slot->host[sizeof(slot->host) - 1] = '\0';
                        slot->in_flight = true;
                        slot->done      = false;
                        slot->success   = false;
                        ip_addr_set_zero(&slot->address);
                        start_query = true;
                    }
                }
                portEXIT_CRITICAL(&dns_mux);
                if (!slot) {
                    return false;  // all fixed resolver slots still own an in-flight callback
                }

                bool pending = false;
                if (snapshot_dns_result(*slot, host, destination, pending)) {
                    return true;
                }
                if (!pending && !start_query) {
                    // A previous negative result is retryable.  Re-arm the same
                    // fixed slot without allocating a callback context.
                    portENTER_CRITICAL(&dns_mux);
                    if (!slot->in_flight) {
                        slot->in_flight = true;
                        slot->done      = false;
                        slot->success   = false;
                        start_query     = true;
                    }
                    portEXIT_CRITICAL(&dns_mux);
                }

                if (start_query) {
                    ip_addr_t immediate {};
                    const err_t result = dns_gethostbyname(slot->host, &immediate, dns_found, slot);
                    if (result == ERR_OK) {
                        complete_dns_slot(*slot, &immediate);
                    } else if (result != ERR_INPROGRESS) {
                        complete_dns_slot(*slot, nullptr);
                    }
                }

                for (;;) {
                    if (snapshot_dns_result(*slot, host, destination, pending)) {
                        return true;
                    }
                    if (!pending) {
                        return false;
                    }
                    const uint32_t remaining = remaining_ms(start_ms, timeout_ms, millis());
                    if (remaining == 0) {
                        return false;
                    }
                    const uint32_t poll_ms = remaining < 10u ? remaining : 10u;
                    TickType_t ticks = pdMS_TO_TICKS(poll_ms);
                    if (ticks == 0) {
                        ticks = 1;
                    }
                    vTaskDelay(ticks);
                }
            }

            class PlainClient {
                WiFiClient _client;

            public:
                uint32_t now_ms() const { return millis(); }
                void     idle() { vTaskDelay(1); }

                bool connect(const ParsedUrl& parsed, uint32_t timeout_ms) {
                    const uint32_t start_ms = millis();
                    IPAddress      address;
                    if (!resolve_host(parsed.host, timeout_ms, address)) {
                        return false;
                    }
                    const uint32_t remaining = remaining_ms(start_ms, timeout_ms, millis());
                    return remaining != 0 && _client.connect(address, parsed.port, static_cast<int32_t>(remaining)) == 1;
                }

                size_t write(const uint8_t* data, size_t size, uint32_t timeout_ms) {
                    const int descriptor = _client.fd();
                    if (descriptor < 0 || !data || !size || timeout_ms == 0) {
                        return 0;
                    }
                    fd_set write_set;
                    FD_ZERO(&write_set);
                    FD_SET(descriptor, &write_set);
                    timeval timeout {
                        static_cast<long>(timeout_ms / 1000u),
                        static_cast<long>((timeout_ms % 1000u) * 1000u),
                    };
                    const int selected = select(descriptor + 1, nullptr, &write_set, nullptr, &timeout);
                    if (selected <= 0 || !FD_ISSET(descriptor, &write_set)) {
                        return 0;
                    }
                    const int sent = send(descriptor, data, size, MSG_DONTWAIT);
                    if (sent < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            _client.stop();
                        }
                        return 0;
                    }
                    return static_cast<size_t>(sent);
                }

                int  available() { return _client.available(); }
                int  read() { return _client.read(); }
                bool connected() { return _client.connected() != 0; }
                void stop() { _client.stop(); }
            };

            class SecureClient {
                WiFiClientSecure _client;

            public:
                uint32_t now_ms() const { return millis(); }
                void     idle() { vTaskDelay(1); }

                bool connect(const ParsedUrl& parsed, uint32_t timeout_ms) {
                    const uint32_t start_ms = millis();
                    IPAddress      address;
                    if (!resolve_host(parsed.host, timeout_ms, address)) {
                        return false;
                    }
                    const uint32_t remaining = remaining_ms(start_ms, timeout_ms, millis());
                    // Arduino-ESP32 2.x exposes TLS socket and handshake timeouts
                    // in whole seconds.  Floor rather than ceil keeps this call
                    // inside the caller's single global deadline.
                    const uint32_t seconds = secure_phase_timeout_seconds(remaining);
                    if (seconds == 0) {
                        return false;
                    }
                    _client.setInsecure();
                    _client.setTimeout(seconds);
                    _client.setHandshakeTimeout(seconds);
                    return _client.connect(address, parsed.port, parsed.host, nullptr, nullptr, nullptr) == 1;
                }

                size_t write(const uint8_t* data, size_t size, uint32_t timeout_ms) {
                    const uint32_t seconds = timeout_ms / 1000u;
                    if (seconds == 0 || !data || !size) {
                        return 0;
                    }
                    _client.setTimeout(seconds);
                    return _client.write(data, size);
                }

                int  available() { return _client.available(); }
                int  read() { return _client.read(); }
                bool connected() { return _client.connected() != 0; }
                void stop() { _client.stop(); }
            };
        }  // namespace
    }  // namespace OutputUrlHttp
}  // namespace WebUI

extern "C" bool fluidnc_output_url_http_get(const char* url, uint32_t timeout_ms, uint16_t* http_status) {
    if (http_status) {
        *http_status = 0;
    }
    if (WebUI::OutputUrlHttp::provider_busy.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    WebUI::OutputUrlHttp::BusyGuard busy_guard;

    try {
        WebUI::OutputUrlHttp::ParsedUrl parsed {};
        if (!WebUI::OutputUrlHttp::parse_url(url, parsed) || timeout_ms == 0 || timeout_ms >= 0x80000000u ||
            WiFi.status() != WL_CONNECTED) {
            return false;
        }
        if (parsed.secure) {
            WebUI::OutputUrlHttp::SecureClient client;
            return WebUI::OutputUrlHttp::perform_get_noexcept(client, parsed, timeout_ms, http_status);
        }
        WebUI::OutputUrlHttp::PlainClient client;
        return WebUI::OutputUrlHttp::perform_get_noexcept(client, parsed, timeout_ms, http_status);
    } catch (...) {
        if (http_status) {
            *http_status = 0;
        }
        return false;
    }
}

#else

// The configured URL module is WiFi-only, but non-ESP32 WiFi targets do not
// share Arduino-ESP32's synchronous client/lwIP API. Keep their forced-link
// provider fail-closed until an equivalent bounded adapter is reviewed.
extern "C" bool fluidnc_output_url_http_get(const char*, uint32_t, uint16_t* http_status) {
    if (http_status) {
        *http_status = 0;
    }
    return false;
}

#endif
