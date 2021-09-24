#pragma once

/**
 * Created by Nandana Perera
 * Git: nxPerera
 */


#include <cstdint>
#include "Planner.h"

enum class MotionDirection : int8_t {
    Nutral      =  0,     // Not sure what happened before :)
    Positive    =  1,     // moved cordinate value increasing direction 
    Negative    = -1,     // moved cordinate value decreasing direction
};

typedef struct {
    float           prev_target;
    MotionDirection prev_direction;
    uint8_t         backlash_enable;
} backlash_data_t;

extern backlash_data_t backlash_data[MAX_N_AXIS];  
void backlash_ini();
void backlash_Reset();
void backlash_Reset_after_probe();
void backlash_Reset_after_homecycle();
void backlash_Reset_systemMotion(int32_t* sys_pos);
void backlash_Reset_for_homing(bool approach, uint8_t homing_mask);
bool backlash_Compensate_befor_target(float* target, plan_line_data_t *pl_data);

