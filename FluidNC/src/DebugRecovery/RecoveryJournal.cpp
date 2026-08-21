#include "RecoveryJournal.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace DebugRecovery {
    namespace {
        class FixedBufferWriter {
        public:
            FixedBufferWriter(char* output, size_t capacity) : _output(output), _capacity(capacity) {
                if (_capacity != 0) {
                    _output[0] = '\0';
                }
            }

            void append(const char* format, ...) {
                if (_failed || _length >= _capacity) {
                    _failed = true;
                    return;
                }
                va_list args;
                va_start(args, format);
                const int count = std::vsnprintf(_output + _length, _capacity - _length, format, args);
                va_end(args);
                if (count < 0 || static_cast<size_t>(count) >= _capacity - _length) {
                    _failed = true;
                    return;
                }
                _length += static_cast<size_t>(count);
            }

            size_t finish() {
                if (!_failed) {
                    return _length;
                }
                if (_capacity != 0) {
                    _output[0] = '\0';
                }
                return 0;
            }

        private:
            char*  _output;
            size_t _capacity;
            size_t _length = 0;
            bool   _failed = false;
        };

        const char* phase_name(BootPhase phase) {
            switch (phase) {
                case BootPhase::Booting:
                    return "booting";
                case BootPhase::LoadingConfig:
                    return "loading-config";
                case BootPhase::ConfigLoaded:
                    return "config-loaded";
                case BootPhase::Runtime:
                    return "runtime";
                default:
                    return "unknown";
            }
        }

        const char* restart_reason_name(DebugRestartReason reason) {
            return reason == DebugRestartReason::GatewayTimeout ? "gateway-timeout" : "none";
        }
    }  // namespace

    size_t serialize_journal_entry(char* output,
                                   size_t capacity,
                                   uint32_t resetReason,
                                   bool previousRecordValid,
                                   const RecoveryRecord& previousRecord,
                                   const BacktraceSnapshot& backtrace) {
        if (output == nullptr || capacity == 0) {
            return 0;
        }

        FixedBufferWriter writer(output, capacity);
        writer.append("{\"event\":\"debug-recovery\",\"resetReason\":%" PRIu32
                      ",\"recordValid\":%s,\"bootSequence\":%" PRIu32
                      ",\"previousPhase\":\"%s\",\"requestedRestart\":\"%s\",\"machineState\":%u"
                      ",\"wifiStatus\":%u,\"pingFailures\":%u,\"configCrc\":\"%08" PRIx32
                      "\",\"configSize\":%" PRIu32 ",\"freeHeap\":%" PRIu32
                      ",\"minimumFreeHeap\":%" PRIu32 ",\"largestFreeBlock\":%" PRIu32
                      ",\"gateway\":\"%08" PRIx32 "\",\"gatewaySuccessAgeMs\":%" PRIu32
                      ",\"uptimeMs\":%" PRIu32 ",\"backtrace\":{\"available\":%s",
                      resetReason,
                      previousRecordValid ? "true" : "false",
                      previousRecord.bootSequence,
                      phase_name(previousRecord.phase),
                      restart_reason_name(previousRecord.requestedRestart),
                      previousRecord.machineState,
                      previousRecord.wifiStatus,
                      previousRecord.pingFailures,
                      previousRecord.configCrc,
                      previousRecord.configSize,
                      previousRecord.freeHeap,
                      previousRecord.minimumFreeHeap,
                      previousRecord.largestFreeBlock,
                      previousRecord.gatewayAddress,
                      previousRecord.gatewaySuccessAgeMs,
                      previousRecord.uptimeMs,
                      backtrace.available ? "true" : "false");

        if (backtrace.available) {
            writer.append(",\"pc\":\"%08" PRIx32 "\",\"excvaddr\":\"%08" PRIx32
                          "\",\"exccause\":%" PRIu32 ",\"addresses\":[",
                          backtrace.pc,
                          backtrace.excvaddr,
                          backtrace.exccause);
            for (size_t index = 0; index < backtrace.addressCount; ++index) {
                writer.append(index == 0 ? "\"%08" PRIx32 "\"" : ",\"%08" PRIx32 "\"", backtrace.addresses[index]);
            }
            writer.append("]");
        }
        writer.append("}}\n");
        return writer.finish();
    }

    bool journal_should_truncate(size_t existingBytes, size_t nextEntryBytes, size_t maximumBytes) {
        if (nextEntryBytes > maximumBytes) {
            return true;
        }
        return existingBytes > maximumBytes - nextEntryBytes;
    }
}  // namespace DebugRecovery
