// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2023 -	Stefan de Bruijn: Modified for listeners.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// Execution states and alarm
#include "State.h"
#include "Probe.h"
#include "Config.h"  // MAX_N_AXIS
#include <map>
#include <vector>

extern const std::map<State, const char*> StateName;

// Step segment generator state flags.
struct StepControl {
    uint8_t endMotion : 1;
    uint8_t executeHold : 1;
    uint8_t executeSysMotion : 1;
    uint8_t updateSpindleSpeed : 1;
};

// System suspend flags. Used in various ways to manage suspend states and procedures.
struct SuspendBits {
    uint8_t holdComplete : 1;     // Indicates initial feed hold is complete.
    uint8_t restartRetract : 1;   // Flag to indicate a retract from a restore parking motion.
    uint8_t retractComplete : 1;  // (Safety door only) Indicates retraction and de-energizing is complete.
    uint8_t initiateRestore : 1;  // (Safety door only) Flag to initiate resume procedures from a cycle start.
    uint8_t restoreComplete : 1;  // (Safety door only) Indicates ready to resume normal operation.
    uint8_t safetyDoorAjar : 1;   // Tracks safety door state for resuming.
    uint8_t motionCancel : 1;     // Indicates a canceled resume motion. Currently used by probing routine.
    uint8_t jogCancel : 1;        // Indicates a jog cancel in process and to reset buffers when complete.
};
union Suspend {
    uint8_t     value;
    SuspendBits bit;
};

enum class SystemDirty {
    None = 0,

    State                = 1 << 0,
    Abort                = 1 << 1,
    Suspend              = 1 << 2,
    FeedOverride         = 1 << 3,
    RapidOverride        = 1 << 4,
    SpindleSpeedOverride = 1 << 5,
    OverrideControl      = 1 << 6,
    SpindleSpeed         = 1 << 7,

    All = ((1 << 16) - 1),
};

// Global system variables

class system_t;
using SystemChangeHandler = void (*)(SystemDirty changes, const system_t& state, void* userData);

class SystemChangeHandlerPair {
public:
    SystemChangeHandler handler_;
    void*               data_;
};

class system_t {
protected:
    volatile State state_;              // Tracks the current system state
    bool           abort_;              // System abort flag. Forces exit back to main loop for reset.
    Suspend        suspend_;            // System suspend bitflag variable that manages holds, cancels, and safety door.
    Percent        f_override_;         // Feed rate override value in percent
    Percent        r_override_;         // Rapids override value in percent
    Percent        spindle_speed_ovr_;  // Spindle speed value in percent
    Override       override_ctrl_;      // Tracks override control states.
    SpindleSpeed   spindle_speed_;

    volatile SystemDirty dirty_ = SystemDirty::All;

    std::vector<SystemChangeHandlerPair> changeHandlers_;

public:
    system_t() { reset(); }

    // Step_control appears to be a status variable that is used like an internal temporary variable. That's why this isn't
    // somewhere in a getter/setter here.
    StepControl step_control;  // Governs the step segment generator depending on system state.

    void reset() {
        memset(&step_control, 0, sizeof(step_control));  // step_control is a POD so this is okay.
        state_ = State::Idle;
        abort_ = false;
        memset(&suspend_, 0, sizeof(suspend_));  // suspend is a POD so this is okay.
        f_override_        = 0;
        r_override_        = 0;
        spindle_speed_ovr_ = 0;
        override_ctrl_     = Override(0);
        spindle_speed_     = 0;
    }

    void register_change_handler(SystemChangeHandler handler, void* userData) {
        SystemChangeHandlerPair pair;
        pair.handler_ = handler;
        pair.data_    = userData;
        changeHandlers_.push_back(pair);
    }

    State IRAM_ATTR state() const { return state_; }
    void IRAM_ATTR  set_state(State value) {
         // We don't have to check the old value always... just when state changes can happen a lot or at more or less the same time.
        // It's also not too bad if there are concurrency issues; it will just trigger more events.
        if (value != state_) {
             dirty_ = SystemDirty(int(dirty_) | int(SystemDirty::State));
             state_ = value;
        }
    }

    bool IRAM_ATTR abort() const { return abort_; }
    void IRAM_ATTR set_abort(bool value) {
        dirty_ = SystemDirty(int(dirty_) | int(SystemDirty::Abort));
        abort_ = value;
    }

    Suspend IRAM_ATTR suspend() const { return suspend_; }
    void IRAM_ATTR    set_suspend(Suspend value) {
           dirty_   = SystemDirty(int(dirty_) | int(SystemDirty::Suspend));
           suspend_ = value;
    }

    Percent IRAM_ATTR f_override() const { return f_override_; }
    void IRAM_ATTR    set_f_override(Percent value) {
           dirty_      = SystemDirty(int(dirty_) | int(SystemDirty::FeedOverride));
           f_override_ = value;
    }

    Percent IRAM_ATTR r_override() const { return r_override_; }
    void IRAM_ATTR    set_r_override(Percent value) {
           dirty_      = SystemDirty(int(dirty_) | int(SystemDirty::RapidOverride));
           r_override_ = value;
    }

    void IRAM_ATTR set_spindle_speed_ovr(uint32_t value) {
        dirty_             = SystemDirty(int(dirty_) | int(SystemDirty::SpindleSpeedOverride));
        spindle_speed_ovr_ = value;
    }
    Percent IRAM_ATTR spindle_speed_ovr() const { return spindle_speed_ovr_; }

    Override IRAM_ATTR override_ctrl() const { return override_ctrl_; }
    void IRAM_ATTR     set_override_ctrl(Override value) {
            dirty_         = SystemDirty(int(dirty_) | int(SystemDirty::OverrideControl));
            override_ctrl_ = value;
    }

    SpindleSpeed IRAM_ATTR spindle_speed() const { return spindle_speed_; }
    void IRAM_ATTR         set_spindle_speed(SpindleSpeed value) {
                if (spindle_speed_ != value) {
                    dirty_         = SystemDirty(int(dirty_) | int(SystemDirty::SpindleSpeed));
                    spindle_speed_ = value;
        }
    }

    void process_changes() {
        if (dirty_ != SystemDirty::None) {
            for (auto& it : changeHandlers_) {
                it.handler_(dirty_, *this, it.data_);
            }
            dirty_ = SystemDirty::None;
        }
    }
};

extern system_t sys;

typedef int32_t steps_t;

// NOTE: These position variables may need to be declared as volatiles, if problems arise.
extern steps_t steps[MAX_N_AXIS];        // Real-time machine (aka home) position vector in steps.
extern steps_t probe_steps[MAX_N_AXIS];  // Last probe position in machine coordinates and steps.

void system_reset();

// Per axis
float   steps_to_motor_pos(steps_t steps, size_t motor);
steps_t motor_pos_to_steps(float mpos, size_t motor);
steps_t get_axis_steps(axis_t axis);
void    set_steps(axis_t axis, steps_t steps);
void    set_motor_pos(size_t motor, float motor_pos);

// All axes
steps_t* get_steps();
void     steps_to_motor_pos(float* motor_pos, steps_t* steps);
void     motor_pos_to_steps(steps_t* steps, float* motor_pos);
void     get_steps(steps_t* steps);
float*   get_motor_pos();
void     set_motor_pos(float* motor_pos, size_t n_motors);
void     steps_to_mpos(float* position, steps_t* steps);

float* get_mpos();
float* get_wco();

bool inMotionState();  // True if moving, i.e. the stepping engine is active
