// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Platform.h"
#include "Config.h"
#include "Configuration/GenericFactory.h"
#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"
#include "CoolantControl.h"
#include "Kinematics/Kinematics.h"
#include "Extenders/Extenders.h"
#include "Control.h"
#include "Probe.h"
#include "Parking.h"
#include "SDCard.h"
#include "Spindles/Spindle.h"
#include "Stepping.h"
#include "Stepper.h"
#include "UartChannel.h"
#include "Driver/Console.h"
#include "Module.h"
#include "Listeners/SysListener.h"
#include "Axes.h"
#include "SPIBus.h"
#include "I2CBus.h"
#include "I2SOBus.h"
#include "UserOutputs.h"
#include "UserInputs.h"
#include "Macros.h"

#include <string_view>

namespace Machine {
    using ::Kinematics::Kinematics;

    class Start : public Configuration::Configurable {
    public:
        bool _mustHome          = true;
        bool _deactivateParking = false;

        // At power-up or a reset, the limit switches will be checked
        // to ensure they are not already active. If so, and hard
        // limits are enabled, Alarm state will be entered instead of
        // Idle and the user will be told to check the limits.
        bool _checkLimits = true;

    public:
        Start() {}

        void group(Configuration::HandlerBase& handler) {
            handler.item("must_home", _mustHome);
            handler.item("deactivate_parking", _deactivateParking);
            handler.item("check_limits", _checkLimits);
        }

        ~Start() = default;
    };

    class MachineConfig : public Configuration::Configurable {
    public:
        MachineConfig() = default;

        Axes*       _axes       = nullptr;
        Kinematics* _kinematics = nullptr;
        SPIBus*     _spi        = nullptr;
#if MAX_N_I2C
        I2CBus* _i2c[MAX_N_I2C] = { nullptr };
#endif
#if MAX_N_I2SO
        I2SOBus* _i2so = nullptr;
#endif
        Stepping*       _stepping    = nullptr;
        CoolantControl* _coolant     = nullptr;
        Probe*          _probe       = nullptr;
        Control*        _control     = nullptr;
        UserOutputs*    _userOutputs = nullptr;
        UserInputs*     _userInputs  = nullptr;
        SDCard*         _sdCard      = nullptr;
        Macros*         _macros      = nullptr;
        Start*          _start       = nullptr;
        Parking*        _parking     = nullptr;

        //        Listeners::SysListenerList _sysListeners;
        //        Spindles::SpindleList      _spindles;
        Extenders::Extenders* _extenders = nullptr;

        UartChannel* _uart_channels[MAX_N_UARTS] = { nullptr };
        Uart*        _uarts[MAX_N_UARTS]         = { nullptr };

        float _arcTolerance      = 0.002f;
        float _junctionDeviation = 0.01f;
        bool  _verboseErrors     = true;
        bool  _reportInches      = false;

        int32_t _planner_blocks = 16;

        // Enables a special set of M-code commands that enables and disables the parking motion.
        // These are controlled by `M56`, `M56 P1`, or `M56 Px` to enable and `M56 P0` to disable.
        // The command is modal and will be set after a planner sync. Since it is GCode, it is
        // executed in sync with GCode commands. It is not a real-time command.
        bool _enableParkingOverrideControl = false;

        // Tracks and reports gcode line numbers. Disabled by default.
        bool _useLineNumbers = false;

        std::string _board = "None";
        std::string _name  = "None";
        std::string _meta  = "";
#if 1
        static MachineConfig*& instance() {
            static MachineConfig* instance = nullptr;
            return instance;
        }
#endif

        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;

        static void load();
        static void load_file(std::string_view file);
        static void load_yaml(std::string_view yaml_string);

        ~MachineConfig();
    };
}

extern Machine::MachineConfig* config;

template <typename D, typename S>
void copyAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] = src[axis];
    }
}

template <typename D, typename S>
void copyAxes(D* dest, S* src) {
    copyAxes(dest, src, Axes::_numberAxis);
}

template <typename D, typename S>
void addAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] += src[axis];
    }
}

template <typename D, typename S>
void addAxes(D* dest, S* src) {
    addAxes(dest, src, Axes::_numberAxis);
}

template <typename D, typename S>
void subtractAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] -= src[axis];
    }
}

template <typename D, typename S>
void subtractAxes(D* dest, S* src) {
    subtractAxes(dest, src, Axes::_numberAxis);
}
