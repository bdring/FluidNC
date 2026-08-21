#include "RecoveryPolicy.h"

namespace DebugRecovery {
    ConfigRecoveryDecision config_recovery_decision(bool wasCrashReset, BootPhase previousPhase) {
        if (wasCrashReset && previousPhase == BootPhase::LoadingConfig) {
            return ConfigRecoveryDecision::LoadBuiltInDefault;
        }
        return ConfigRecoveryDecision::LoadConfiguredFile;
    }

    SupervisorTimingSample capture_supervisor_timing(const std::atomic<uint32_t>& lastGatewaySuccessMs, SupervisorClock readNowMs) {
        const uint32_t observedLastGatewaySuccessMs = lastGatewaySuccessMs.load(std::memory_order_relaxed);
        const uint32_t nowMs                        = readNowMs();
        return SupervisorTimingSample { observedLastGatewaySuccessMs, nowMs };
    }

    bool automatic_filesystem_format_allowed() {
        return false;
    }

    bool supervisor_start_failure_should_restart() {
        return false;
    }

    SupervisorDecision supervisor_decision(uint32_t nowMs, uint32_t lastGatewaySuccessMs, uint32_t deadlineMs) {
        const uint32_t elapsed = nowMs - lastGatewaySuccessMs;
        return elapsed >= deadlineMs ? SupervisorDecision::RestartController : SupervisorDecision::ContinueMonitoring;
    }

    SupervisorDecision supervisor_decision(uint32_t nowMs,
                                           uint32_t lastGatewaySuccessMs,
                                           uint32_t deadlineMs,
                                           const DebugRuntimeState& observedState) {
        // DEBUG-ONLY: stale job/homing/spindle state must not suppress recovery.
        (void)observedState;
        return supervisor_decision(nowMs, lastGatewaySuccessMs, deadlineMs);
    }
}  // namespace DebugRecovery
