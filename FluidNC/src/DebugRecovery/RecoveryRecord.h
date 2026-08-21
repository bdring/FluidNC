#pragma once

#include "RecoveryPolicy.h"

#include <cstdint>

namespace DebugRecovery {
    enum class DebugRestartReason : uint8_t {
        None = 0,
        GatewayTimeout,
    };

    struct RecoveryRecord {
        uint32_t magic;
        uint16_t version;
        uint16_t recordSize;
        uint32_t crc;
        uint32_t bootSequence;
        BootPhase phase;
        DebugRestartReason requestedRestart;
        uint8_t machineState;
        uint8_t wifiStatus;
        uint8_t pingFailures;
        uint8_t reserved[3];
        uint32_t configCrc;
        uint32_t configSize;
        uint32_t freeHeap;
        uint32_t minimumFreeHeap;
        uint32_t largestFreeBlock;
        uint32_t gatewayAddress;
        uint32_t gatewaySuccessAgeMs;
        uint32_t uptimeMs;
    };

    RecoveryRecord make_recovery_record(uint32_t previousBootSequence);
    void seal_recovery_record(RecoveryRecord& record);
    bool recovery_record_valid(const RecoveryRecord& record);
    bool should_persist_previous_boot(bool wasUnexpectedReset,
                                      bool previousRecordValid,
                                      DebugRestartReason requestedRestart);
}  // namespace DebugRecovery
