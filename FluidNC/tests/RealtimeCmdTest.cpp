#include <gtest/gtest.h>
#include "../src/RealtimeCmd.h"

// Test Cmd enum values - ensure they match spec
TEST(RealtimeCmd, NoneValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::None), 0);
}

TEST(RealtimeCmd, ResetValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Reset), 0x18);  // Ctrl-X
}

TEST(RealtimeCmd, StatusReportValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::StatusReport), '?');
}

TEST(RealtimeCmd, CycleStartValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::CycleStart), '~');
}

TEST(RealtimeCmd, FeedHoldValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedHold), '!');
}

// Extended ASCII commands start at 0x80
TEST(RealtimeCmd, SafetyDoorValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SafetyDoor), 0x84);
}

TEST(RealtimeCmd, JogCancelValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::JogCancel), 0x85);
}

TEST(RealtimeCmd, DebugReportValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::DebugReport), 0x86);
}

// Macro commands
TEST(RealtimeCmd, Macro0Value) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Macro0), 0x87);
}

TEST(RealtimeCmd, Macro1Value) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Macro1), 0x88);
}

TEST(RealtimeCmd, Macro2Value) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Macro2), 0x89);
}

TEST(RealtimeCmd, Macro3Value) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Macro3), 0x8a);
}

// Feed override commands
TEST(RealtimeCmd, FeedOvrResetValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedOvrReset), 0x90);
}

TEST(RealtimeCmd, FeedOvrCoarsePlusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedOvrCoarsePlus), 0x91);
}

TEST(RealtimeCmd, FeedOvrCoarseMinusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedOvrCoarseMinus), 0x92);
}

TEST(RealtimeCmd, FeedOvrFinePlusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedOvrFinePlus), 0x93);
}

TEST(RealtimeCmd, FeedOvrFineMinusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedOvrFineMinus), 0x94);
}

// Rapid override commands
TEST(RealtimeCmd, RapidOvrResetValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RapidOvrReset), 0x95);
}

TEST(RealtimeCmd, RapidOvrMediumValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RapidOvrMedium), 0x96);
}

TEST(RealtimeCmd, RapidOvrLowValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RapidOvrLow), 0x97);
}

TEST(RealtimeCmd, RapidOvrExtraLowValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::RapidOvrExtraLow), 0x98);
}

// Spindle override commands
TEST(RealtimeCmd, SpindleOvrResetValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrReset), 0x99);
}

TEST(RealtimeCmd, SpindleOvrCoarsePlusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrCoarsePlus), 0x9A);
}

TEST(RealtimeCmd, SpindleOvrCoarseMinusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrCoarseMinus), 0x9B);
}

TEST(RealtimeCmd, SpindleOvrFinePlusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrFinePlus), 0x9C);
}

TEST(RealtimeCmd, SpindleOvrFineMinusValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrFineMinus), 0x9D);
}

TEST(RealtimeCmd, SpindleOvrStopValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::SpindleOvrStop), 0x9E);
}

// Coolant override commands
TEST(RealtimeCmd, CoolantFloodOvrToggleValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::CoolantFloodOvrToggle), 0xA0);
}

TEST(RealtimeCmd, CoolantMistOvrToggleValue) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::CoolantMistOvrToggle), 0xA1);
}

// Test basic commands are ASCII printable
TEST(RealtimeCmd, BasicCommandsArePrintable) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::StatusReport), '?');
    EXPECT_EQ(static_cast<uint8_t>(Cmd::CycleStart), '~');
    EXPECT_EQ(static_cast<uint8_t>(Cmd::FeedHold), '!');
}

// Test Reset is Ctrl-X
TEST(RealtimeCmd, ResetIsCtrlX) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::Reset), 0x18);
}

// Test command value ordering
TEST(RealtimeCmd, CommandValueOrdering) {
    // Printable commands should be less than extended ASCII
    EXPECT_LT(static_cast<uint8_t>(Cmd::StatusReport), 0x80);
    EXPECT_LT(static_cast<uint8_t>(Cmd::CycleStart), 0x80);
    EXPECT_LT(static_cast<uint8_t>(Cmd::FeedHold), 0x80);
}

// Test extended ASCII commands are >= 0x80
TEST(RealtimeCmd, ExtendedAsciiCommands) {
    EXPECT_GE(static_cast<uint8_t>(Cmd::SafetyDoor), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::JogCancel), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::FeedOvrReset), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::RapidOvrReset), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::SpindleOvrReset), 0x80);
}

// Test command groupings
TEST(RealtimeCmd, FeedOverrideSequence) {
    // Feed override commands should be sequential
    uint8_t reset = static_cast<uint8_t>(Cmd::FeedOvrReset);
    uint8_t coarse_plus = static_cast<uint8_t>(Cmd::FeedOvrCoarsePlus);
    uint8_t coarse_minus = static_cast<uint8_t>(Cmd::FeedOvrCoarseMinus);
    uint8_t fine_plus = static_cast<uint8_t>(Cmd::FeedOvrFinePlus);
    uint8_t fine_minus = static_cast<uint8_t>(Cmd::FeedOvrFineMinus);
    
    EXPECT_EQ(coarse_plus, reset + 1);
    EXPECT_EQ(coarse_minus, reset + 2);
    EXPECT_EQ(fine_plus, reset + 3);
    EXPECT_EQ(fine_minus, reset + 4);
}

TEST(RealtimeCmd, RapidOverrideSequence) {
    // Rapid override commands should be sequential
    uint8_t reset = static_cast<uint8_t>(Cmd::RapidOvrReset);
    uint8_t medium = static_cast<uint8_t>(Cmd::RapidOvrMedium);
    uint8_t low = static_cast<uint8_t>(Cmd::RapidOvrLow);
    uint8_t extra_low = static_cast<uint8_t>(Cmd::RapidOvrExtraLow);
    
    EXPECT_EQ(medium, reset + 1);
    EXPECT_EQ(low, reset + 2);
    EXPECT_EQ(extra_low, reset + 3);
}

TEST(RealtimeCmd, SpindleOverrideSequence) {
    // Spindle override commands should be sequential
    uint8_t reset = static_cast<uint8_t>(Cmd::SpindleOvrReset);
    uint8_t coarse_plus = static_cast<uint8_t>(Cmd::SpindleOvrCoarsePlus);
    uint8_t coarse_minus = static_cast<uint8_t>(Cmd::SpindleOvrCoarseMinus);
    uint8_t fine_plus = static_cast<uint8_t>(Cmd::SpindleOvrFinePlus);
    uint8_t fine_minus = static_cast<uint8_t>(Cmd::SpindleOvrFineMinus);
    uint8_t stop = static_cast<uint8_t>(Cmd::SpindleOvrStop);
    
    EXPECT_EQ(coarse_plus, reset + 1);
    EXPECT_EQ(coarse_minus, reset + 2);
    EXPECT_EQ(fine_plus, reset + 3);
    EXPECT_EQ(fine_minus, reset + 4);
    EXPECT_EQ(stop, reset + 5);
}

TEST(RealtimeCmd, MacroSequence) {
    // Macro commands should be sequential
    uint8_t macro0 = static_cast<uint8_t>(Cmd::Macro0);
    uint8_t macro1 = static_cast<uint8_t>(Cmd::Macro1);
    uint8_t macro2 = static_cast<uint8_t>(Cmd::Macro2);
    uint8_t macro3 = static_cast<uint8_t>(Cmd::Macro3);
    
    EXPECT_EQ(macro1, macro0 + 1);
    EXPECT_EQ(macro2, macro0 + 2);
    EXPECT_EQ(macro3, macro0 + 3);
}

// Test command type is uint8_t
TEST(RealtimeCmd, CmdIsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<Cmd>, uint8_t>, "Cmd must be uint8_t");
}

// Test command character classifications
TEST(RealtimeCmd, BasicCommandsAreLowAscii) {
    // Reset, StatusReport, CycleStart, FeedHold are below 0x80
    EXPECT_LT(static_cast<uint8_t>(Cmd::Reset), 0x80);
    EXPECT_LT(static_cast<uint8_t>(Cmd::StatusReport), 0x80);
    EXPECT_LT(static_cast<uint8_t>(Cmd::CycleStart), 0x80);
    EXPECT_LT(static_cast<uint8_t>(Cmd::FeedHold), 0x80);
}

// Test override commands are in high extended ASCII
TEST(RealtimeCmd, OverrideCommandsAreHighAscii) {
    EXPECT_GE(static_cast<uint8_t>(Cmd::SafetyDoor), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::FeedOvrReset), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::RapidOvrReset), 0x80);
    EXPECT_GE(static_cast<uint8_t>(Cmd::SpindleOvrReset), 0x80);
}

// Test all defined commands have unique values
TEST(RealtimeCmd, AllCommandsUnique) {
    std::set<uint8_t> command_values;
    command_values.insert(static_cast<uint8_t>(Cmd::None));
    command_values.insert(static_cast<uint8_t>(Cmd::Reset));
    command_values.insert(static_cast<uint8_t>(Cmd::StatusReport));
    command_values.insert(static_cast<uint8_t>(Cmd::CycleStart));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedHold));
    command_values.insert(static_cast<uint8_t>(Cmd::SafetyDoor));
    command_values.insert(static_cast<uint8_t>(Cmd::JogCancel));
    command_values.insert(static_cast<uint8_t>(Cmd::DebugReport));
    command_values.insert(static_cast<uint8_t>(Cmd::Macro0));
    command_values.insert(static_cast<uint8_t>(Cmd::Macro1));
    command_values.insert(static_cast<uint8_t>(Cmd::Macro2));
    command_values.insert(static_cast<uint8_t>(Cmd::Macro3));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedOvrReset));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedOvrCoarsePlus));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedOvrCoarseMinus));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedOvrFinePlus));
    command_values.insert(static_cast<uint8_t>(Cmd::FeedOvrFineMinus));
    command_values.insert(static_cast<uint8_t>(Cmd::RapidOvrReset));
    command_values.insert(static_cast<uint8_t>(Cmd::RapidOvrMedium));
    command_values.insert(static_cast<uint8_t>(Cmd::RapidOvrLow));
    command_values.insert(static_cast<uint8_t>(Cmd::RapidOvrExtraLow));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrReset));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrCoarsePlus));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrCoarseMinus));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrFinePlus));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrFineMinus));
    command_values.insert(static_cast<uint8_t>(Cmd::SpindleOvrStop));
    command_values.insert(static_cast<uint8_t>(Cmd::CoolantFloodOvrToggle));
    command_values.insert(static_cast<uint8_t>(Cmd::CoolantMistOvrToggle));
    
    // All 29 commands should be unique
    EXPECT_EQ(command_values.size(), 29);
}

// Test command ranges
TEST(RealtimeCmd, ControlCharacterCommands) {
    // Reset is a control character
    EXPECT_LE(static_cast<uint8_t>(Cmd::Reset), 0x1F);
}

TEST(RealtimeCmd, MacroCommandRange) {
    uint8_t macro0 = static_cast<uint8_t>(Cmd::Macro0);
    uint8_t macro3 = static_cast<uint8_t>(Cmd::Macro3);
    EXPECT_GE(macro0, 0x80);
    EXPECT_LE(macro3, 0xFF);
}

TEST(RealtimeCmd, CoolantCommandsAreHighExtendedAscii) {
    uint8_t flood = static_cast<uint8_t>(Cmd::CoolantFloodOvrToggle);
    uint8_t mist = static_cast<uint8_t>(Cmd::CoolantMistOvrToggle);
    EXPECT_GE(flood, 0xA0);
    EXPECT_GE(mist, 0xA0);
}

// Test override increment sequence pattern
TEST(RealtimeCmd, OverrideIncrementPatterns) {
    // For feed overrides: Reset, CoarsePlus, CoarseMinus, FinePlus, FineMinus
    // Pattern repeats for Rapid and Spindle
    uint8_t feed_reset = static_cast<uint8_t>(Cmd::FeedOvrReset);
    uint8_t rapid_reset = static_cast<uint8_t>(Cmd::RapidOvrReset);
    uint8_t spindle_reset = static_cast<uint8_t>(Cmd::SpindleOvrReset);
    
    // Each group has the same offset pattern
    EXPECT_EQ(rapid_reset - feed_reset, 5);
    EXPECT_EQ(spindle_reset - feed_reset, 9);
}

// Test that None command is safe default
TEST(RealtimeCmd, NoneIsDefaultCommand) {
    EXPECT_EQ(static_cast<uint8_t>(Cmd::None), 0);
}
