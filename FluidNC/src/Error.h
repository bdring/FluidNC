#pragma once

/*
  Error.h - Error numbers
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P
        2020 - Mitch Bradley

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <map>

#include <cstdint>

// Grbl error codes. Valid values (0-255)
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
    FsFailedMount               = 60,  // SD Failed to mount
    FsFailedRead                = 61,  // SD Failed to read file
    FsFailedOpenDir             = 62,  // SD card failed to open directory
    FsDirNotFound               = 63,  // SD Card directory not found
    FsFileEmpty                 = 64,  // SD Card directory not found
    FsFileNotFound              = 65,  // SD Card file not found
    FsFailedOpenFile            = 66,  // SD card failed to open file
    FsFailedBusy                = 67,  // SD card is busy
    FsFailedDelDir              = 68,
    FsFailedDelFile             = 69,
    BtFailBegin                 = 70,  // Bluetooth failed to start
    WifiFailBegin               = 71,  // WiFi failed to start
    NumberRange                 = 80,  // Setting number range problem
    InvalidValue                = 81,  // Setting string problem
    FsFailedCreateFile          = 82,
    MessageFailed               = 90,
    NvsSetFailed                = 100,
    NvsGetStatsFailed           = 101,
    AuthenticationFailed        = 110,
    Eol                         = 111,
    Eof                         = 112,  // Not necessarily an error
    AnotherInterfaceBusy        = 120,
    JogCancelled                = 130,
    BadPinSpecification         = 150,
    BadRuntimeConfigSetting     = 151,
    ConfigurationInvalid        = 152,
};

const char* errorString(Error errorNumber);

extern std::map<Error, const char*> ErrorNames;
