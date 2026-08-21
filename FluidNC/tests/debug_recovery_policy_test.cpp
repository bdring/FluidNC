#include "DebugRecovery/RecoveryPolicy.h"
#include "DebugRecovery/RecoveryJournal.h"
#include "DebugRecovery/RecoveryRecord.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

namespace {
int failures = 0;
std::atomic<uint32_t>* simulatedLastGatewaySuccess = nullptr;

uint32_t update_gateway_success_during_clock_read() {
    simulatedLastGatewaySuccess->store(10'001u, std::memory_order_relaxed);
    return 10'000u;
}

template <typename T>
void expect_equal(const char* name, T actual, T expected) {
    if (actual == expected) {
        return;
    }
    std::cerr << "FAIL " << name << ": actual=";
    if constexpr (std::is_enum_v<T>) {
        std::cerr << static_cast<unsigned>(actual) << " expected=" << static_cast<unsigned>(expected);
    } else {
        std::cerr << actual << " expected=" << expected;
    }
    std::cerr << '\n';
    ++failures;
}
}  // namespace

int main() {
    using DebugRecovery::BootPhase;
    using DebugRecovery::ConfigRecoveryDecision;
    using DebugRecovery::SupervisorDecision;

    expect_equal("normal boot loads configured file",
                 DebugRecovery::config_recovery_decision(false, BootPhase::LoadingConfig),
                 ConfigRecoveryDecision::LoadConfiguredFile);
    expect_equal("panic while loading config uses built-in fallback",
                 DebugRecovery::config_recovery_decision(true, BootPhase::LoadingConfig),
                 ConfigRecoveryDecision::LoadBuiltInDefault);
    expect_equal("runtime panic reloads configured file",
                 DebugRecovery::config_recovery_decision(true, BootPhase::Runtime),
                 ConfigRecoveryDecision::LoadConfiguredFile);
    expect_equal("post-config panic reloads configured file",
                 DebugRecovery::config_recovery_decision(true, BootPhase::ConfigLoaded),
                 ConfigRecoveryDecision::LoadConfiguredFile);
    expect_equal("unknown-phase panic reloads configured file",
                 DebugRecovery::config_recovery_decision(true, BootPhase::Unknown),
                 ConfigRecoveryDecision::LoadConfiguredFile);

    expect_equal("supervisor waits before deadline",
                 DebugRecovery::supervisor_decision(59'999, 0, 60'000),
                 SupervisorDecision::ContinueMonitoring);
    expect_equal("supervisor restarts exactly at deadline",
                 DebugRecovery::supervisor_decision(60'000, 0, 60'000),
                 SupervisorDecision::RestartController);
    expect_equal("supervisor deadline handles millis wraparound",
                 DebugRecovery::supervisor_decision(0x00000020u, 0xfffffff0u, 48u),
                 SupervisorDecision::RestartController);
    const DebugRecovery::DebugRuntimeState stuckRuntimeState {
        true,
        true,
        true,
    };
    expect_equal("debug supervisor ignores stale job homing and spindle bits at deadline",
                 DebugRecovery::supervisor_decision(60'000, 0, 60'000, stuckRuntimeState),
                 SupervisorDecision::RestartController);

    std::atomic<uint32_t> sharedLastGatewaySuccess { 9'999u };
    simulatedLastGatewaySuccess                = &sharedLastGatewaySuccess;
    const auto timing = DebugRecovery::capture_supervisor_timing(sharedLastGatewaySuccess,
                                                                  update_gateway_success_during_clock_read);
    expect_equal("timing capture keeps the last success observed before the clock read",
                 timing.lastGatewaySuccessMs,
                 uint32_t { 9'999 });
    expect_equal("timing capture records the clock value from the same ordered sample", timing.nowMs, uint32_t { 10'000 });
    expect_equal("a concurrent newer ping cannot underflow into a false timeout",
                 DebugRecovery::supervisor_decision(timing.nowMs, timing.lastGatewaySuccessMs, 60'000),
                 SupervisorDecision::ContinueMonitoring);
    expect_equal("debug recovery never formats LocalFS automatically",
                 DebugRecovery::automatic_filesystem_format_allowed(),
                 false);
    expect_equal("supervisor task allocation failure does not enter an immediate reboot loop",
                 DebugRecovery::supervisor_start_failure_should_restart(),
                 false);

    auto record = DebugRecovery::make_recovery_record(40);
    expect_equal("new RTC record increments boot sequence", record.bootSequence, uint32_t { 41 });
    expect_equal("new RTC record starts in booting phase", record.phase, BootPhase::Booting);
    expect_equal("new RTC record validates after sealing", DebugRecovery::recovery_record_valid(record), true);

    record.freeHeap = 1234;
    expect_equal("mutated RTC record fails CRC", DebugRecovery::recovery_record_valid(record), false);
    DebugRecovery::seal_recovery_record(record);
    expect_equal("resealed RTC record validates", DebugRecovery::recovery_record_valid(record), true);

    expect_equal("ordinary reset without requested recovery is not journaled",
                 DebugRecovery::should_persist_previous_boot(false, true, DebugRecovery::DebugRestartReason::None),
                 false);
    expect_equal("panic is journaled",
                 DebugRecovery::should_persist_previous_boot(true, true, DebugRecovery::DebugRestartReason::None),
                 true);
    expect_equal("unexpected reset is journaled without a valid RTC record",
                 DebugRecovery::should_persist_previous_boot(true, false, DebugRecovery::DebugRestartReason::None),
                 true);
    expect_equal("requested gateway restart is journaled",
                 DebugRecovery::should_persist_previous_boot(false, true, DebugRecovery::DebugRestartReason::GatewayTimeout),
                 true);
    expect_equal("invalid RTC record cannot claim requested restart",
                 DebugRecovery::should_persist_previous_boot(false, false, DebugRecovery::DebugRestartReason::GatewayTimeout),
                 false);

    auto journalRecord             = DebugRecovery::make_recovery_record(6);
    journalRecord.phase            = BootPhase::Runtime;
    journalRecord.requestedRestart = DebugRecovery::DebugRestartReason::GatewayTimeout;
    journalRecord.machineState     = 3;
    journalRecord.wifiStatus       = 2;
    journalRecord.pingFailures     = 12;
    journalRecord.configCrc        = 0x1234abcdu;
    journalRecord.configSize       = 6946;
    journalRecord.freeHeap         = 10000;
    journalRecord.minimumFreeHeap  = 7000;
    journalRecord.largestFreeBlock = 4096;
    journalRecord.gatewayAddress   = 0xc0a81e01u;
    journalRecord.gatewaySuccessAgeMs = 60000;
    journalRecord.uptimeMs            = 123456;
    DebugRecovery::seal_recovery_record(journalRecord);

    const uint32_t addresses[] = { 0x400d1234u, 0x400d5678u };
    const DebugRecovery::BacktraceSnapshot backtrace {
        true,
        0x400d1234u,
        0x3ffb0000u,
        28u,
        addresses,
        2u,
    };
    char journalLine[1024] {};
    const size_t journalLength =
        DebugRecovery::serialize_journal_entry(journalLine, sizeof(journalLine), 4u, true, journalRecord, backtrace);
    const std::string expectedJournal =
        "{\"event\":\"debug-recovery\",\"resetReason\":4,\"recordValid\":true,\"bootSequence\":7,"
        "\"previousPhase\":\"runtime\",\"requestedRestart\":\"gateway-timeout\",\"machineState\":3,"
        "\"wifiStatus\":2,\"pingFailures\":12,\"configCrc\":\"1234abcd\",\"configSize\":6946,"
        "\"freeHeap\":10000,\"minimumFreeHeap\":7000,\"largestFreeBlock\":4096,"
        "\"gateway\":\"c0a81e01\",\"gatewaySuccessAgeMs\":60000,\"uptimeMs\":123456,"
        "\"backtrace\":{\"available\":true,\"pc\":\"400d1234\",\"excvaddr\":\"3ffb0000\","
        "\"exccause\":28,\"addresses\":[\"400d1234\",\"400d5678\"]}}\n";
    expect_equal("journal serialization succeeds", journalLength, expectedJournal.size());
    expect_equal("journal serialization is deterministic", std::string(journalLine), expectedJournal);
    expect_equal("journal remains append-only below cap",
                 DebugRecovery::journal_should_truncate(16000, 300, 16384),
                 false);
    expect_equal("journal truncates before exceeding cap",
                 DebugRecovery::journal_should_truncate(16000, 500, 16384),
                 true);
    expect_equal("journal rejects undersized output buffer",
                 DebugRecovery::serialize_journal_entry(journalLine, 20, 4u, true, journalRecord, backtrace),
                 size_t { 0 });

    if (failures != 0) {
        return 1;
    }
    std::cout << "PASS debug recovery policy\n";
    return 0;
}
