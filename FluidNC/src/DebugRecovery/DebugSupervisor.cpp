#include "DebugSupervisor.h"

#ifdef ESP_PLATFORM

#    include "DebugRecovery.h"
#    include "RecoveryPolicy.h"

#    include "Config.h"
#    include "Driver/restart.h"
#    include "Driver/watchdog.h"
#    include "GCode.h"
#    include "Job.h"
#    include "Logging.h"
#    include "System.h"

#    include <WiFi.h>
#    include <ping/ping_sock.h>
#    include <esp_heap_caps.h>
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    include <lwip/ip_addr.h>

#    include <algorithm>
#    include <atomic>
#    include <limits>

namespace DebugRecovery {
    namespace {
        constexpr uint32_t gatewayPingIntervalMs = 5000;
        constexpr uint32_t gatewayPingTimeoutMs  = 1000;
        constexpr uint32_t gatewayDeadlineMs     = 60000;
        constexpr uint32_t supervisorPeriodMs    = 1000;

        TaskHandle_t      supervisorTask         = nullptr;
        esp_ping_handle_t pingSession            = nullptr;
        std::atomic<uint32_t> lastGatewaySuccessMs { 0 };
        std::atomic<uint8_t>  consecutivePingFailures { 0 };

        void ping_success(esp_ping_handle_t, void*) {
            lastGatewaySuccessMs.store(millis(), std::memory_order_relaxed);
            consecutivePingFailures.store(0, std::memory_order_relaxed);
        }

        void ping_timeout(esp_ping_handle_t, void*) {
            uint8_t observed = consecutivePingFailures.load(std::memory_order_relaxed);
            while (observed != std::numeric_limits<uint8_t>::max() &&
                   !consecutivePingFailures.compare_exchange_weak(
                       observed, static_cast<uint8_t>(observed + 1u), std::memory_order_relaxed)) {
            }
        }

        void ping_end(esp_ping_handle_t, void*) {}

        uint32_t gateway_value(const IPAddress& gateway) {
            return (static_cast<uint32_t>(gateway[0]) << 24u) | (static_cast<uint32_t>(gateway[1]) << 16u) |
                   (static_cast<uint32_t>(gateway[2]) << 8u) | static_cast<uint32_t>(gateway[3]);
        }

        bool gateway_is_valid(const IPAddress& gateway) {
            return gateway_value(gateway) != 0;
        }

        bool start_ping_session(const IPAddress& gateway) {
            if (!gateway_is_valid(gateway)) {
                return false;
            }

            esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
            config.count             = ESP_PING_COUNT_INFINITE;
            config.interval_ms       = gatewayPingIntervalMs;
            config.timeout_ms        = gatewayPingTimeoutMs;
            config.data_size         = 32;
            IP4_ADDR(ip_2_ip4(&config.target_addr), gateway[0], gateway[1], gateway[2], gateway[3]);
            IP_SET_TYPE_VAL(config.target_addr, IPADDR_TYPE_V4);

            esp_ping_callbacks_t callbacks {};
            callbacks.on_ping_success = ping_success;
            callbacks.on_ping_timeout = ping_timeout;
            callbacks.on_ping_end     = ping_end;

            if (esp_ping_new_session(&config, &callbacks, &pingSession) != ESP_OK) {
                pingSession = nullptr;
                return false;
            }
            if (esp_ping_start(pingSession) != ESP_OK) {
                esp_ping_delete_session(pingSession);
                pingSession = nullptr;
                return false;
            }
            log_warn("DEBUG-ONLY gateway watchdog is pinging " << gateway.toString().c_str()
                                                                << " and will reboot after " << gatewayDeadlineMs
                                                                << " ms without a reply");
            return true;
        }

        HealthSnapshot health_snapshot(uint32_t nowMs, uint32_t observedLastGatewaySuccessMs, const IPAddress& gateway) {
            const uint32_t successAge = nowMs - observedLastGatewaySuccessMs;
            return HealthSnapshot {
                static_cast<uint8_t>(sys.state()),
                static_cast<uint8_t>(WiFi.status()),
                consecutivePingFailures.load(std::memory_order_relaxed),
                static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
                static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                gateway_value(gateway),
                successAge,
                nowMs,
            };
        }

        void supervisor_loop(void*) {
            add_watchdog_to_task();
            lastGatewaySuccessMs.store(millis(), std::memory_order_relaxed);
            uint32_t lastPingStartAttemptMs = lastGatewaySuccessMs.load(std::memory_order_relaxed) - gatewayPingIntervalMs;

            for (;;) {
                const SupervisorTimingSample timing = capture_supervisor_timing(lastGatewaySuccessMs, []() -> uint32_t { return millis(); });
                const uint32_t nowMs              = timing.nowMs;
                const IPAddress gateway = WiFi.gatewayIP();
                if (pingSession == nullptr && nowMs - lastPingStartAttemptMs >= gatewayPingIntervalMs) {
                    lastPingStartAttemptMs = nowMs;
                    start_ping_session(gateway);
                }

                const HealthSnapshot snapshot = health_snapshot(nowMs, timing.lastGatewaySuccessMs, gateway);
                record_health(snapshot);

                const DebugRuntimeState observedState {
                    Job::active(),
                    state_is(State::Homing),
                    gc_state.modal.spindle != SpindleState::Disable,
                };
                if (supervisor_decision(nowMs, timing.lastGatewaySuccessMs, gatewayDeadlineMs, observedState) ==
                    SupervisorDecision::RestartController) {
                    prepare_debug_restart(DebugRestartReason::GatewayTimeout, consecutivePingFailures.load(std::memory_order_relaxed));
                    log_error("DEBUG-ONLY gateway watchdog timeout; forcing ESP32 restart regardless of machine state"
                              << " state=" << static_cast<unsigned>(snapshot.machineState)
                              << " job=" << observedState.jobActive << " homing=" << observedState.homing
                              << " spindle=" << observedState.spindleRequested << " heap=" << snapshot.freeHeap);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    restart();
                }

                feed_watchdog();
                vTaskDelay(pdMS_TO_TICKS(supervisorPeriodMs));
            }
        }
    }  // namespace

    void start_debug_supervisor() {
        if (supervisorTask != nullptr) {
            return;
        }
        const BaseType_t created = xTaskCreateAffinitySet(supervisor_loop,
                                                          "debug-supervisor",
                                                          4096,
                                                          nullptr,
                                                          1,
                                                          (1 << SUPPORT_TASK_CORE),
                                                          &supervisorTask);
        if (created != pdPASS) {
            if (supervisor_start_failure_should_restart()) {
                prepare_debug_restart(DebugRestartReason::GatewayTimeout, 0);
                log_error("DEBUG-ONLY gateway watchdog task could not start; forcing ESP32 restart");
                restart();
            }
            log_error("DEBUG-ONLY gateway watchdog task could not start; continuing without it to avoid a reboot loop");
        }
    }
}  // namespace DebugRecovery

#else

namespace DebugRecovery {
    void start_debug_supervisor() {}
}

#endif
