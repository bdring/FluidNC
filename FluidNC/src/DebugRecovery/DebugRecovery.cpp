#include "DebugRecovery.h"
#include "RecoveryJournal.h"

#ifdef ESP_PLATFORM

#    include "Driver/backtrace.h"
#    include "FileStream.h"
#    include "Logging.h"

#    include <esp_attr.h>
#    include <esp_system.h>

#    include <algorithm>
#    include <cstring>

namespace DebugRecovery {
    namespace {
        RTC_NOINIT_ATTR RecoveryRecord rtcRecord;
        RecoveryRecord                 previousRecord {};
        bool                           previousRecordValid = false;
        bool                           previousResetWasCrash = false;
        BootPhase                      previousBootPhase   = BootPhase::Unknown;

        constexpr const char* crashJournalPath = "debug-crash-journal.jsonl";
        constexpr size_t      crashJournalLimit = 16u * 1024u;

        uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
            for (size_t index = 0; index < length; ++index) {
                crc ^= data[index];
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
                }
            }
            return crc;
        }

        void seal_rtc_record() {
            seal_recovery_record(rtcRecord);
        }

        bool reset_was_crash(esp_reset_reason_t reason) {
            switch (reason) {
                case ESP_RST_PANIC:
                case ESP_RST_INT_WDT:
                case ESP_RST_TASK_WDT:
                case ESP_RST_WDT:
                    return true;
                default:
                    return false;
            }
        }

        bool reset_should_be_journaled(esp_reset_reason_t reason) {
            return reset_was_crash(reason) || reason == ESP_RST_BROWNOUT;
        }

        void persist_previous_boot(esp_reset_reason_t resetReason) {
            const DebugRestartReason requestedReason =
                previousRecordValid ? previousRecord.requestedRestart : DebugRestartReason::None;
            if (!should_persist_previous_boot(reset_should_be_journaled(resetReason),
                                              previousRecordValid,
                                              requestedReason)) {
                return;
            }

            RecoveryRecord recordForJournal {};
            if (previousRecordValid) {
                recordForJournal = previousRecord;
            } else {
                recordForJournal.phase = BootPhase::Unknown;
            }

            backtrace_t backtraceRecord {};
            const bool  backtraceAvailable = reset_was_crash(resetReason) && backtrace_get(&backtraceRecord);
            const BacktraceSnapshot backtrace {
                backtraceAvailable,
                backtraceRecord.pc,
                backtraceRecord.excvaddr,
                backtraceRecord.exccause,
                backtraceRecord.addresses,
                backtraceRecord.num_addresses,
            };

            char         journalLine[1024];
            const size_t journalLength = serialize_journal_entry(journalLine,
                                                                  sizeof(journalLine),
                                                                  static_cast<uint32_t>(resetReason),
                                                                  previousRecordValid,
                                                                  recordForJournal,
                                                                  backtrace);
            if (journalLength == 0) {
                log_error("Debug recovery journal entry exceeded its fixed buffer");
                return;
            }

            try {
                size_t existingBytes = 0;
                try {
                    FileStream existing(crashJournalPath, "rb", LocalFS);
                    existingBytes = existing.size();
                } catch (...) {
                    existingBytes = 0;
                }
                const char* mode = journal_should_truncate(existingBytes, journalLength, crashJournalLimit) ? "wb" : "ab";
                FileStream  journal(crashJournalPath, mode, LocalFS);
                if (journal.write(reinterpret_cast<const uint8_t*>(journalLine), journalLength) != journalLength) {
                    log_error("Debug recovery journal write was incomplete");
                    return;
                }
                log_info("Saved debug recovery evidence to " << crashJournalPath);
            } catch (...) {
                log_error("Could not persist debug recovery journal");
            }
        }
    }  // namespace

    void initialize_after_localfs_mount() {
        const esp_reset_reason_t resetReason = esp_reset_reason();
        previousResetWasCrash               = reset_was_crash(resetReason);
        previousRecordValid = recovery_record_valid(rtcRecord);
        if (previousRecordValid) {
            previousRecord   = rtcRecord;
            previousBootPhase = previousRecord.phase;
        } else {
            std::memset(&previousRecord, 0, sizeof(previousRecord));
            previousBootPhase = BootPhase::Unknown;
        }

        persist_previous_boot(resetReason);

        const uint32_t previousSequence = previousRecordValid ? previousRecord.bootSequence : 0u;
        rtcRecord                       = make_recovery_record(previousSequence);
    }

    uint32_t current_boot_sequence() {
        return rtcRecord.bootSequence;
    }

    bool previous_reset_was_crash() {
        return previousResetWasCrash;
    }

    BootPhase previous_phase() {
        return previousBootPhase;
    }

    void mark_phase(BootPhase phase) {
        rtcRecord.phase = phase;
        seal_rtc_record();
    }

    void record_config_identity(const char* filename) {
        rtcRecord.configCrc  = 0;
        rtcRecord.configSize = 0;
        try {
            FileStream file(filename, "rb", LocalFS);
            uint8_t    buffer[256];
            uint32_t   crc       = 0xffffffffu;
            size_t     remaining = file.size();
            rtcRecord.configSize = remaining;
            while (remaining != 0) {
                const size_t chunk = std::min(remaining, sizeof(buffer));
                const int    count = file.read(reinterpret_cast<char*>(buffer), chunk);
                if (count <= 0) {
                    rtcRecord.configSize = 0;
                    crc                  = 0xffffffffu;
                    break;
                }
                crc = crc32_update(crc, buffer, count);
                remaining -= count;
            }
            rtcRecord.configCrc = crc ^ 0xffffffffu;
        } catch (...) {
            log_warn("Debug recovery could not fingerprint configuration file " << filename);
        }
        seal_rtc_record();
    }

    void record_health(const HealthSnapshot& snapshot) {
        rtcRecord.machineState         = snapshot.machineState;
        rtcRecord.wifiStatus           = snapshot.wifiStatus;
        rtcRecord.pingFailures         = snapshot.pingFailures;
        rtcRecord.freeHeap             = snapshot.freeHeap;
        rtcRecord.minimumFreeHeap      = snapshot.minimumFreeHeap;
        rtcRecord.largestFreeBlock     = snapshot.largestFreeBlock;
        rtcRecord.gatewayAddress       = snapshot.gatewayAddress;
        rtcRecord.gatewaySuccessAgeMs  = snapshot.gatewaySuccessAgeMs;
        rtcRecord.uptimeMs             = snapshot.uptimeMs;
        seal_rtc_record();
    }

    void prepare_debug_restart(DebugRestartReason reason, uint8_t pingFailures) {
        rtcRecord.requestedRestart = reason;
        rtcRecord.pingFailures     = pingFailures;
        seal_rtc_record();
    }
}  // namespace DebugRecovery

#else

namespace DebugRecovery {
    void initialize_after_localfs_mount() {}
    bool previous_reset_was_crash() {
        return false;
    }
    BootPhase previous_phase() {
        return BootPhase::Unknown;
    }
    void mark_phase(BootPhase) {}
    void record_config_identity(const char*) {}
    void record_health(const HealthSnapshot&) {}
    void prepare_debug_restart(DebugRestartReason, uint8_t) {}
}  // namespace DebugRecovery

#endif
