// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2020 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <map>
#include <cstdint>

// Error codes. Valid values (0-255)
enum class Error : uint8_t {
    Ok                          = 0,
    ExpectedCommandLetter       = 1,
    BadNumberFormat             = 2,
    InvalidStatement            = 3,
    NegativeValue               = 4,
    SettingDisabled             = 5,
    SettingStepPulseMin         = 6,
    SettingReadFail             = 7,
    IdleError                   = 8,
    SystemGcLock                = 9,
    SoftLimitError              = 10,
    Overflow                    = 11,
    MaxStepRateExceeded         = 12,
    CheckDoor                   = 13,
    LineLengthExceeded          = 14,
    TravelExceeded              = 15,
    InvalidJogCommand           = 16,
    SettingDisabledLaser        = 17,
    HomingNoCycles              = 18,
    SingleAxisHoming            = 19,
    GcodeUnsupportedCommand     = 20,
    GcodeModalGroupViolation    = 21,
    GcodeUndefinedFeedRate      = 22,
    GcodeCommandValueNotInteger = 23,
    GcodeAxisCommandConflict    = 24,
    GcodeWordRepeated           = 25,
    GcodeNoAxisWords            = 26,
    GcodeInvalidLineNumber      = 27,
    GcodeValueWordMissing       = 28,
    GcodeUnsupportedCoordSys    = 29,
    GcodeG53InvalidMotionMode   = 30,
    GcodeAxisWordsExist         = 31,
    GcodeNoAxisWordsInPlane     = 32,
    GcodeInvalidTarget          = 33,
    GcodeArcRadiusError         = 34,
    GcodeNoOffsetsInPlane       = 35,
    GcodeUnusedWords            = 36,
    GcodeG43DynamicAxisError    = 37,
    GcodeMaxValueExceeded       = 38,
    PParamMaxExceeded           = 39,
    CheckControlPins            = 40,
    FsFailedMount               = 60,  // Filesystem failed to mount
    FsFailedRead                = 61,  // Failed to read file
    FsFailedOpenDir             = 62,  // Failed to open directory
    FsDirNotFound               = 63,  // Directory not found
    FsFileEmpty                 = 64,  // File is empty
    FsFileNotFound              = 65,  // File not found
    FsFailedOpenFile            = 66,  // Failed to open file
    FsFailedBusy                = 67,  // Filesystem is busy
    FsFailedDelDir              = 68,
    FsFailedDelFile             = 69,
    FsFailedRenameFile          = 70,
    NumberRange                 = 80,  // Setting number range problem
    InvalidValue                = 81,  // Setting string problem
    FsFailedCreateFile          = 82,
    FsFailedFormat              = 83,
    MessageFailed               = 90,
    NvsSetFailed                = 100,
    NvsGetStatsFailed           = 101,
    AuthenticationFailed        = 110,
    Eol                         = 111,
    Eof                         = 112,  // Not necessarily an error
    Reset                       = 113,
    AnotherInterfaceBusy        = 120,
    JogCancelled                = 130,
    BadPinSpecification         = 150,
    BadRuntimeConfigSetting     = 151,
    ConfigurationInvalid        = 152,
    UploadFailed                = 160,
    DownloadFailed              = 161,
    ReadOnlySetting             = 162,
};

const char* errorString(Error errorNumber);

extern std::map<Error, const char*> ErrorNames;
