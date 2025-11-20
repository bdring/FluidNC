#pragma once

// Alarm codes.
enum class ExecAlarm : uint8_t {
    None                  = 0,
    HardLimit             = 1,
    SoftLimit             = 2,
    AbortCycle            = 3,
    ProbeFailInitial      = 4,
    ProbeFailContact      = 5,
    HomingFailReset       = 6,
    HomingFailDoor        = 7,
    HomingFailPulloff     = 8,
    HomingFailApproach    = 9,
    SpindleControl        = 10,
    StartupPin            = 11,  // control or limit input pin active
    HomingAmbiguousSwitch = 12,
    HardStop              = 13,
    Unhomed               = 14,
    Init                  = 15,
    ExpanderReset         = 16,
    GCodeError            = 17,
    ProbeHardLimit        = 18,
};

extern volatile ExecAlarm lastAlarm;
