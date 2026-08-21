#pragma once

#include <atomic>
#include <cstdint>

namespace DebugRecovery {
    enum class BootPhase : uint8_t {
        Unknown = 0,
        Booting,
        LoadingConfig,
        ConfigLoaded,
        Runtime,
    };

    enum class ConfigRecoveryDecision : uint8_t {
        LoadConfiguredFile = 0,
        LoadBuiltInDefault,
    };

    enum class SupervisorDecision : uint8_t {
        ContinueMonitoring = 0,
        RestartController,
    };

    struct DebugRuntimeState {
        bool jobActive;
        bool homing;
        bool spindleRequested;
    };

    struct SupervisorTimingSample {
        uint32_t lastGatewaySuccessMs;
        uint32_t nowMs;
    };

    using SupervisorClock = uint32_t (*)();

    ConfigRecoveryDecision config_recovery_decision(bool wasCrashReset, BootPhase previousPhase);
    SupervisorTimingSample capture_supervisor_timing(const std::atomic<uint32_t>& lastGatewaySuccessMs, SupervisorClock readNowMs);
    bool automatic_filesystem_format_allowed();
    bool supervisor_start_failure_should_restart();
    SupervisorDecision supervisor_decision(uint32_t nowMs, uint32_t lastGatewaySuccessMs, uint32_t deadlineMs);
    SupervisorDecision supervisor_decision(uint32_t nowMs,
                                           uint32_t lastGatewaySuccessMs,
                                           uint32_t deadlineMs,
                                           const DebugRuntimeState& observedState);
}  // namespace DebugRecovery
