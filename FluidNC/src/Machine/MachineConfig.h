// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Assert.h"
#include "../Configuration/GenericFactory.h"
#include "../Configuration/HandlerBase.h"
#include "../Configuration/Configurable.h"
#include "../CoolantControl.h"
#include "../Kinematics/Kinematics.h"
#include "../WebUI/BTConfig.h"
#include "../Control.h"
#include "../Probe.h"
#include "src/Parking.h"
#include "../SDCard.h"
#include "../Spindles/Spindle.h"
#include "../Stepping.h"
#include "../Stepper.h"
#include "../Config.h"
#include "../OLED.h"
#include "Axes.h"
#include "SPIBus.h"
#include "I2CBus.h"
#include "I2SOBus.h"
#include "UserOutputs.h"
#include "Macros.h"

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
        bool _checkLimits = false;

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

        Axes*                 _axes           = nullptr;
        Kinematics*           _kinematics     = nullptr;
        SPIBus*               _spi            = nullptr;
        I2CBus*               _i2c[MAX_N_I2C] = { nullptr };
        I2SOBus*              _i2so           = nullptr;
        Stepping*             _stepping       = nullptr;
        CoolantControl*       _coolant        = nullptr;
        Probe*                _probe          = nullptr;
        Control*              _control        = nullptr;
        UserOutputs*          _userOutputs    = nullptr;
        SDCard*               _sdCard         = nullptr;
        Macros*               _macros         = nullptr;
        Start*                _start          = nullptr;
        Parking*              _parking        = nullptr;
        OLED*                 _oled           = nullptr;
        Spindles::SpindleList _spindles;

        UartChannel* _uart_channels[MAX_N_UARTS] = { nullptr };
        Uart*        _uarts[MAX_N_UARTS]         = { nullptr };

        float _arcTolerance      = 0.002f;
        float _junctionDeviation = 0.01f;
        bool  _verboseErrors     = false;
        bool  _reportInches      = false;

        size_t _planner_blocks = 16;

        // Enables a special set of M-code commands that enables and disables the parking motion.
        // These are controlled by `M56`, `M56 P1`, or `M56 Px` to enable and `M56 P0` to disable.
        // The command is modal and will be set after a planner sync. Since it is GCode, it is
        // executed in sync with GCode commands. It is not a real-time command.
        bool _enableParkingOverrideControl = false;

        // Tracks and reports gcode line numbers. Disabled by default.
        bool _useLineNumbers = false;

        String _board = "None";
        String _name  = "None";
        String _meta  = "";
#if 1
        static MachineConfig*& instance() {
            static MachineConfig* instance = nullptr;
            return instance;
        }
#endif

        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;

        static bool load();
        static bool load(const char* file);
        static bool load(StringRange* input);

        ~MachineConfig();
    };
}

extern Machine::MachineConfig* config;

template <typename T>
void copyAxes(T* dest, const T* src) {
    auto n_axis = config->_axes->_numberAxis;
    for (size_t axis = 0; axis < n_axis; axis++) {
        dest[axis] = src[axis];
    }
}
