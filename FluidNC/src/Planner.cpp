// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2011 Jens Geisler
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Planner.cpp - Thin wrapper providing backward-compatible free functions
  
  The actual implementation is in the Planner/ subdirectory classes.
  This file delegates to the planner instance from MachineConfig.
*/

#include "Planner.h"
#include "Planner/BasePlanner.h"
#include "Planner/TrapezoidPlanner.h"
#include "Machine/MachineConfig.h"

// Accessor for the planner instance (used by Stepper.cpp)
BasePlanner* getPlanner() {
    if (config) {
        if (!config->_planner) {
            log_info("Defaulting to trapezoid planner");
            config->_planner = new TrapezoidPlanner("trapezoid_planner");
        }
        return config->_planner;
    } else {
        return nullptr;
    }
}

void plan_init() {
    // Initialize the planner from configuration
    auto planner = getPlanner();
    if (planner) {
        planner->init();
    }
}

void plan_reset() {
    auto planner = getPlanner();
    if (planner) {
        planner->reset();
    }
}

void plan_reset_buffer() {
    auto planner = getPlanner();
    if (planner) {
        planner->resetBuffer();
    }
}

bool plan_buffer_line(float* target, plan_line_data_t* pl_data) {
    auto planner = getPlanner();
    if (planner) {
        return planner->bufferLine(target, pl_data);
    }
    return false;
}

void plan_discard_current_block() {
    auto planner = getPlanner();
    if (planner) {
        planner->discardCurrentBlock();
    }
}

plan_block_t* plan_get_system_motion_block() {
    auto planner = getPlanner();
    if (planner) {
        return planner->getSystemMotionBlock();
    }
    return nullptr;
}

plan_block_t* plan_get_current_block() {
    auto planner = getPlanner();
    if (planner) {
        return planner->getCurrentBlock();
    }
    return nullptr;
}

float plan_get_exec_block_exit_speed_sqr() {
    auto planner = getPlanner();
    if (planner) {
        return planner->getExecBlockExitSpeedSqr();
    }
    return 0.0f;
}

uint8_t plan_check_full_buffer() {
    auto planner = getPlanner();
    if (planner) {
        return planner->checkFullBuffer() ? 1 : 0;
    }
    return 0;
}

float plan_compute_profile_nominal_speed(plan_block_t* block) {
    auto planner = getPlanner();
    if (planner) {
        return planner->computeNominalSpeed(block);
    }
    return MINIMUM_FEED_RATE;
}

void plan_update_velocity_profile_parameters() {
    auto planner = getPlanner();
    if (planner) {
        planner->updateVelocityProfileParameters();
    }
}

void plan_sync_position() {
    auto planner = getPlanner();
    if (planner) {
        planner->syncPosition();
    }
}

void plan_cycle_reinitialize() {
    auto planner = getPlanner();
    if (planner) {
        planner->cycleReinitialize();
    }
}

uint8_t plan_get_block_buffer_available() {
    auto planner = getPlanner();
    if (planner) {
        return planner->getBlockBufferAvailable();
    }
    return 0;
}

void plan_get_planner_mpos(float* target) {
    auto planner = getPlanner();
    if (planner) {
        planner->getPlannerMpos(target);
    }
}
