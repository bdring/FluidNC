#include <gtest/gtest.h>
#include "../src/Error.h"
#include <map>

// errorString is defined in ProcessSettings.cpp, but we can't link it
// without bringing in too many dependencies. For now we'll test the
// Error enum values themselves rather than the string mapping.

// Test error code values match expected enum values
TEST(ErrorCode, EnumOk) {
    EXPECT_EQ(static_cast<uint8_t>(Error::Ok), 0);
}

TEST(ErrorCode, EnumExpectedCommandLetter) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpectedCommandLetter), 1);
}

TEST(ErrorCode, EnumBadNumberFormat) {
    EXPECT_EQ(static_cast<uint8_t>(Error::BadNumberFormat), 2);
}

TEST(ErrorCode, EnumInvalidStatement) {
    EXPECT_EQ(static_cast<uint8_t>(Error::InvalidStatement), 3);
}

TEST(ErrorCode, EnumNegativeValue) {
    EXPECT_EQ(static_cast<uint8_t>(Error::NegativeValue), 4);
}

TEST(ErrorCode, EnumSettingDisabled) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SettingDisabled), 5);
}

TEST(ErrorCode, EnumSettingStepPulseMin) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SettingStepPulseMin), 6);
}

TEST(ErrorCode, EnumSettingReadFail) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SettingReadFail), 7);
}

TEST(ErrorCode, EnumIdleError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::IdleError), 8);
}

TEST(ErrorCode, EnumSystemGcLock) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SystemGcLock), 9);
}

TEST(ErrorCode, EnumSoftLimitError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SoftLimitError), 10);
}

TEST(ErrorCode, EnumOverflow) {
    EXPECT_EQ(static_cast<uint8_t>(Error::Overflow), 11);
}

TEST(ErrorCode, EnumMaxStepRateExceeded) {
    EXPECT_EQ(static_cast<uint8_t>(Error::MaxStepRateExceeded), 12);
}

TEST(ErrorCode, EnumCheckDoor) {
    EXPECT_EQ(static_cast<uint8_t>(Error::CheckDoor), 13);
}

TEST(ErrorCode, EnumLineLengthExceeded) {
    EXPECT_EQ(static_cast<uint8_t>(Error::LineLengthExceeded), 14);
}

TEST(ErrorCode, EnumTravelExceeded) {
    EXPECT_EQ(static_cast<uint8_t>(Error::TravelExceeded), 15);
}

TEST(ErrorCode, EnumInvalidJogCommand) {
    EXPECT_EQ(static_cast<uint8_t>(Error::InvalidJogCommand), 16);
}

TEST(ErrorCode, EnumSettingDisabledLaser) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SettingDisabledLaser), 17);
}

TEST(ErrorCode, EnumHomingNoCycles) {
    EXPECT_EQ(static_cast<uint8_t>(Error::HomingNoCycles), 18);
}

TEST(ErrorCode, EnumSingleAxisHoming) {
    EXPECT_EQ(static_cast<uint8_t>(Error::SingleAxisHoming), 19);
}

TEST(ErrorCode, EnumGcodeUnsupportedCommand) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeUnsupportedCommand), 20);
}

TEST(ErrorCode, EnumGcodeModalGroupViolation) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeModalGroupViolation), 21);
}

TEST(ErrorCode, EnumGcodeUndefinedFeedRate) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeUndefinedFeedRate), 22);
}

TEST(ErrorCode, EnumGcodeCommandValueNotInteger) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeCommandValueNotInteger), 23);
}

TEST(ErrorCode, EnumGcodeAxisCommandConflict) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeAxisCommandConflict), 24);
}

TEST(ErrorCode, EnumGcodeWordRepeated) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeWordRepeated), 25);
}

TEST(ErrorCode, EnumGcodeNoAxisWords) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeNoAxisWords), 26);
}

TEST(ErrorCode, EnumGcodeInvalidLineNumber) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeInvalidLineNumber), 27);
}

TEST(ErrorCode, EnumGcodeValueWordMissing) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeValueWordMissing), 28);
}

TEST(ErrorCode, EnumGcodeUnsupportedCoordSys) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeUnsupportedCoordSys), 29);
}

TEST(ErrorCode, EnumGcodeG53InvalidMotionMode) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeG53InvalidMotionMode), 30);
}

TEST(ErrorCode, EnumGcodeAxisWordsExist) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeAxisWordsExist), 31);
}

TEST(ErrorCode, EnumGcodeNoAxisWordsInPlane) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeNoAxisWordsInPlane), 32);
}

TEST(ErrorCode, EnumGcodeInvalidTarget) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeInvalidTarget), 33);
}

TEST(ErrorCode, EnumGcodeArcRadiusError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeArcRadiusError), 34);
}

TEST(ErrorCode, EnumGcodeNoOffsetsInPlane) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeNoOffsetsInPlane), 35);
}

TEST(ErrorCode, EnumGcodeUnusedWords) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeUnusedWords), 36);
}

TEST(ErrorCode, EnumGcodeG43DynamicAxisError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeG43DynamicAxisError), 37);
}

TEST(ErrorCode, EnumGcodeMaxValueExceeded) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeMaxValueExceeded), 38);
}

TEST(ErrorCode, EnumPParamMaxExceeded) {
    EXPECT_EQ(static_cast<uint8_t>(Error::PParamMaxExceeded), 39);
}

TEST(ErrorCode, EnumCheckStartupPins) {
    EXPECT_EQ(static_cast<uint8_t>(Error::CheckStartupPins), 40);
}

TEST(ErrorCode, EnumFsFailedMount) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedMount), 60);
}

TEST(ErrorCode, EnumFsFailedRead) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedRead), 61);
}

TEST(ErrorCode, EnumFsFailedOpenDir) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedOpenDir), 62);
}

TEST(ErrorCode, EnumFsDirNotFound) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsDirNotFound), 63);
}

TEST(ErrorCode, EnumFsFileEmpty) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFileEmpty), 64);
}

TEST(ErrorCode, EnumFsFileNotFound) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFileNotFound), 65);
}

TEST(ErrorCode, EnumFsFailedOpenFile) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedOpenFile), 66);
}

TEST(ErrorCode, EnumFsFailedBusy) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedBusy), 67);
}

TEST(ErrorCode, EnumFsFailedDelDir) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedDelDir), 68);
}

TEST(ErrorCode, EnumFsFailedDelFile) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedDelFile), 69);
}

TEST(ErrorCode, EnumFsFailedRenameFile) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedRenameFile), 70);
}

TEST(ErrorCode, EnumNumberRange) {
    EXPECT_EQ(static_cast<uint8_t>(Error::NumberRange), 80);
}

TEST(ErrorCode, EnumInvalidValue) {
    EXPECT_EQ(static_cast<uint8_t>(Error::InvalidValue), 81);
}

TEST(ErrorCode, EnumFsFailedCreateFile) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedCreateFile), 82);
}

TEST(ErrorCode, EnumFsFailedFormat) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FsFailedFormat), 83);
}

TEST(ErrorCode, EnumMessageFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::MessageFailed), 90);
}

TEST(ErrorCode, EnumNvsSetFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::NvsSetFailed), 100);
}

TEST(ErrorCode, EnumNvsGetStatsFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::NvsGetStatsFailed), 101);
}

TEST(ErrorCode, EnumAuthenticationFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::AuthenticationFailed), 110);
}

TEST(ErrorCode, EnumEol) {
    EXPECT_EQ(static_cast<uint8_t>(Error::Eol), 111);
}

TEST(ErrorCode, EnumEof) {
    EXPECT_EQ(static_cast<uint8_t>(Error::Eof), 112);
}

TEST(ErrorCode, EnumReset) {
    EXPECT_EQ(static_cast<uint8_t>(Error::Reset), 113);
}

TEST(ErrorCode, EnumNoData) {
    EXPECT_EQ(static_cast<uint8_t>(Error::NoData), 114);
}

TEST(ErrorCode, EnumAnotherInterfaceBusy) {
    EXPECT_EQ(static_cast<uint8_t>(Error::AnotherInterfaceBusy), 120);
}

TEST(ErrorCode, EnumJogCancelled) {
    EXPECT_EQ(static_cast<uint8_t>(Error::JogCancelled), 130);
}

TEST(ErrorCode, EnumBadPinSpecification) {
    EXPECT_EQ(static_cast<uint8_t>(Error::BadPinSpecification), 150);
}

TEST(ErrorCode, EnumBadRuntimeConfigSetting) {
    EXPECT_EQ(static_cast<uint8_t>(Error::BadRuntimeConfigSetting), 151);
}

TEST(ErrorCode, EnumConfigurationInvalid) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ConfigurationInvalid), 152);
}

TEST(ErrorCode, EnumUploadFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::UploadFailed), 160);
}

TEST(ErrorCode, EnumDownloadFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::DownloadFailed), 161);
}

TEST(ErrorCode, EnumReadOnlySetting) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ReadOnlySetting), 162);
}

TEST(ErrorCode, EnumExpressionDivideByZero) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionDivideByZero), 170);
}

TEST(ErrorCode, EnumExpressionInvalidArgument) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionInvalidArgument), 171);
}

TEST(ErrorCode, EnumExpressionInvalidResult) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionInvalidResult), 172);
}

TEST(ErrorCode, EnumExpressionUnknownOp) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionUnknownOp), 173);
}

TEST(ErrorCode, EnumExpressionArgumentOutOfRange) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionArgumentOutOfRange), 174);
}

TEST(ErrorCode, EnumExpressionSyntaxError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ExpressionSyntaxError), 175);
}

TEST(ErrorCode, EnumFlowControlSyntaxError) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FlowControlSyntaxError), 176);
}

TEST(ErrorCode, EnumFlowControlNotExecutingMacro) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FlowControlNotExecutingMacro), 177);
}

TEST(ErrorCode, EnumFlowControlOutOfMemory) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FlowControlOutOfMemory), 178);
}

TEST(ErrorCode, EnumFlowControlStackOverflow) {
    EXPECT_EQ(static_cast<uint8_t>(Error::FlowControlStackOverflow), 179);
}

TEST(ErrorCode, EnumParameterAssignmentFailed) {
    EXPECT_EQ(static_cast<uint8_t>(Error::ParameterAssignmentFailed), 180);
}

TEST(ErrorCode, EnumGcodeValueWordInvalid) {
    EXPECT_EQ(static_cast<uint8_t>(Error::GcodeValueWordInvalid), 181);
}

// Test error code ordering and ranges
TEST(ErrorCode, GCodeErrorsAreInRange20To40) {
    EXPECT_GE(static_cast<uint8_t>(Error::GcodeUnsupportedCommand), 20);
    EXPECT_LE(static_cast<uint8_t>(Error::CheckStartupPins), 40);
}

TEST(ErrorCode, FilesystemErrorsAreInRange60To83) {
    EXPECT_GE(static_cast<uint8_t>(Error::FsFailedMount), 60);
    EXPECT_LE(static_cast<uint8_t>(Error::FsFailedFormat), 83);
}

TEST(ErrorCode, ExpressionErrorsAreInRange170To175) {
    EXPECT_GE(static_cast<uint8_t>(Error::ExpressionDivideByZero), 170);
    EXPECT_LE(static_cast<uint8_t>(Error::ExpressionSyntaxError), 175);
}

TEST(ErrorCode, FlowControlErrorsAreInRange176To179) {
    EXPECT_GE(static_cast<uint8_t>(Error::FlowControlSyntaxError), 176);
    EXPECT_LE(static_cast<uint8_t>(Error::FlowControlStackOverflow), 179);
}

// Test error code type is uint8_t
TEST(ErrorCode, ErrorCodeIsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<Error>, uint8_t>, "Error must be uint8_t");
}
