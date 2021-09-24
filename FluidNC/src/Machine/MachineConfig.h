// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Assert.h"
#include "../Configuration/GenericFactory.h"
#include "../Configuration/HandlerBase.h"
#include "../Configuration/Configurable.h"
#include "../CoolantControl.h"
#include "../WebUI/BTConfig.h"
#include "../Control.h"
#include "../Probe.h"
#include "../SDCard.h"
#include "../Spindles/Spindle.h"
#include "../Stepping.h"
#include "../Stepper.h"
#include "../Logging.h"
#include "../Config.h"
#include "../Backlash.h"
#include "Axes.h"
#include "SPIBus.h"
#include "I2SOBus.h"
#include "UserOutputs.h"
#include "Macros.h"

namespace Machine {
    class MachineConfig : public Configuration::Configurable {
    public:
        MachineConfig() = default;

        Axes*           _axes        = nullptr;
        SPIBus*         _spi         = nullptr;
        I2SOBus*        _i2so        = nullptr;
        Stepping*       _stepping    = nullptr;
        CoolantControl* _coolant     = nullptr;
        Probe*          _probe       = nullptr;
        Control*        _control     = nullptr;
        UserOutputs*    _userOutputs = nullptr;
        SDCard*         _sdCard      = nullptr;
        Macros*         _macros      = nullptr;

        Spindles::SpindleList _spindles;

        bool  _laserMode          = false;
        float _arcTolerance       = 0.002f;
        float _junctionDeviation  = 0.01f;
        bool  _verboseErrors      = false;
        bool  _reportInches       = false;
        bool  _homingInitLock     = true;
        int   _softwareDebounceMs = 0;

        // Enables a special set of M-code commands that enables and disables the parking motion.
        // These are controlled by `M56`, `M56 P1`, or `M56 Px` to enable and `M56 P0` to disable.
        // The command is modal and will be set after a planner sync. Since it is GCode, it is
        // executed in sync with GCode commands. It is not a real-time command.
        bool _enableParkingOverrideControl = false;
        bool _deactivateParkingUponInit    = false;

        // At power-up or a reset, the limit switches will be checked to ensure they are not active
        // before initialization. If a problem is detected and hard limits are enabled, Alarm state
        // will be entered instead of Idle, messaging the user to check the limits, rather than idle.
        bool _checkLimitsAtInit = true;

        // If your machine has two limits switches wired in parallel to one axis, you will need to enable
        // this feature. Since the two switches are sharing a single pin, there is no way to tell
        // which one is enabled. This option only effects homing, where if a limit is engaged, the system
        // will alarm and force the user to manually disengage the limit switch. Otherwise, if you have one
        // limit switch for each axis, don't enable this option. By keeping it disabled, you can perform a
        // homing cycle while on the limit switch and not have to move the machine off of it.
        bool _limitsTwoSwitchesOnAxis = false;

        // This option will automatically disable the laser during a feed hold by invoking a spindle stop
        // override immediately after coming to a stop. However, this also means that the laser still may
        // be reenabled by disabling the spindle stop override, if needed. This is purely a safety feature
        // to ensure the laser doesn't inadvertently remain powered while at a stop and cause a fire.
        bool _disableLaserDuringHold = true;

        // Tracks and reports gcode line numbers. Disabled by default.
        bool _useLineNumbers = false;

        String _board = "None";
        String _name  = "None";

#if 1
        static MachineConfig*& instance() {
            static MachineConfig* instance = nullptr;
            return instance;
        }
#endif

        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;

        static size_t readFile(const char* file, char*& buffer);
        static bool   load(const char* file);

        ~MachineConfig();
    };
}

extern Machine::MachineConfig* config;
