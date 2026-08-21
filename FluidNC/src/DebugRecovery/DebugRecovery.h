#pragma once

#include "RecoveryPolicy.h"
#include "RecoveryRecord.h"

#include <cstdint>

namespace DebugRecovery {
    inline constexpr const char diagnostic_hardening_id[] = "v4.0.4-webhardening-20260821-r22";

    struct HealthSnapshot {
        uint8_t machineState;
        uint8_t wifiStatus;
        uint8_t pingFailures;
        uint32_t freeHeap;
        uint32_t minimumFreeHeap;
        uint32_t largestFreeBlock;
        uint32_t gatewayAddress;
        uint32_t gatewaySuccessAgeMs;
        uint32_t uptimeMs;
    };

    void initialize_after_localfs_mount();
    uint32_t current_boot_sequence();
    bool previous_reset_was_crash();
    BootPhase previous_phase();
    void mark_phase(BootPhase phase);
    void record_config_identity(const char* filename);
    void record_health(const HealthSnapshot& snapshot);
    void prepare_debug_restart(DebugRestartReason reason, uint8_t pingFailures);
}  // namespace DebugRecovery
