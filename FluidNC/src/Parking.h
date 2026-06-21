// Copyright (c) 2022 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"

#include "GCode.h"    // CoolantState etc
#include "Planner.h"  // plan_line_data_t

#include <cstdint>

class Parking : public Configuration::Configurable {
private:
    // Configuration
    bool   _enable       = false;
    float  _target_mpos  = -5.0;
    float  _pullout      = 5.0;
    float  _rate         = 800.0;
    float  _pullout_rate = 250.0;
    axis_t _axis         = Z_AXIS;

    // local variables
    float parking_target[MAX_N_AXIS];
    float restore_target[MAX_N_AXIS];
    float retract_waypoint;

    CoolantState saved_coolant;
    SpindleState saved_spindle;
    SpindleSpeed saved_spindle_speed;

    plan_line_data_t plan_data;

    plan_block_t* block;

    void moveto(float* target);

    bool can_park();

public:
    Parking() {}

    void setup();       // Called when suspend start
    void set_target();  // Called when motion has stopped after suspend

    void restore_spindle();  // Restores spindle state upon resume
    void restore_coolant();  // Restores coolant state upon resume

    void park(bool restart);
    void unpark(bool restart);

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    ~Parking() = default;
};
