#include "RecoveryRecord.h"

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace DebugRecovery {
    namespace {
        constexpr uint32_t recoveryMagic   = 0x44524731;  // "DRG1"
        constexpr uint16_t recoveryVersion = 1;

        uint32_t crc32(const uint8_t* data, size_t length) {
            uint32_t crc = 0xffffffffu;
            for (size_t index = 0; index < length; ++index) {
                crc ^= data[index];
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
                }
            }
            return crc ^ 0xffffffffu;
        }

        uint32_t record_crc(RecoveryRecord record) {
            record.crc = 0;
            return crc32(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
        }
    }  // namespace

    static_assert(std::is_trivially_copyable_v<RecoveryRecord>);

    RecoveryRecord make_recovery_record(uint32_t previousBootSequence) {
        RecoveryRecord record {};
        record.magic        = recoveryMagic;
        record.version      = recoveryVersion;
        record.recordSize   = sizeof(RecoveryRecord);
        record.bootSequence = previousBootSequence + 1u;
        record.phase        = BootPhase::Booting;
        seal_recovery_record(record);
        return record;
    }

    void seal_recovery_record(RecoveryRecord& record) {
        record.crc = record_crc(record);
    }

    bool recovery_record_valid(const RecoveryRecord& record) {
        return record.magic == recoveryMagic && record.version == recoveryVersion && record.recordSize == sizeof(RecoveryRecord) &&
               record.crc == record_crc(record);
    }

    bool should_persist_previous_boot(bool wasUnexpectedReset,
                                      bool previousRecordValid,
                                      DebugRestartReason requestedRestart) {
        if (wasUnexpectedReset) {
            return true;
        }
        return previousRecordValid && requestedRestart != DebugRestartReason::None;
    }
}  // namespace DebugRecovery
