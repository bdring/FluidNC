#pragma once

#include "RecoveryRecord.h"

#include <cstddef>
#include <cstdint>

namespace DebugRecovery {
    struct BacktraceSnapshot {
        bool available;
        uint32_t pc;
        uint32_t excvaddr;
        uint32_t exccause;
        const uint32_t* addresses;
        size_t addressCount;
    };

    size_t serialize_journal_entry(char* output,
                                   size_t capacity,
                                   uint32_t resetReason,
                                   bool previousRecordValid,
                                   const RecoveryRecord& previousRecord,
                                   const BacktraceSnapshot& backtrace);
    bool journal_should_truncate(size_t existingBytes, size_t nextEntryBytes, size_t maximumBytes);
}  // namespace DebugRecovery
