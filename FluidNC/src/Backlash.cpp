
/**
 * Created by Nandana Perera
 * Git: nxPerera
 */

#include "Backlash.h"
#include "NutsBolts.h"
#include "Config.h"                 // MAX_N_AXIS
#include "Machine/MachineConfig.h"  // config#include "Machine/MachineConfig.h"  // config

backlash_data_t backlash_data[MAX_N_AXIS];  

void backlash_ini() {   
    for (int i = 0; i < config->_axes->_numberAxis; i++) {
        memset(&backlash_data[i], 0, sizeof(backlash_data_t));  // Clear planner struct
        backlash_data[i].backlash_enable    = true;
        backlash_data[i].prev_direction     = MotionDirection::Nutral; //DIR_NEUTRAL;
        backlash_data[i].prev_target        = 0.0f;
    }
}

//This method check if the intended motion to target require any backlash correction
//if needed it will create intermedeate (secret*) plan_buffer_line() call for the required correction motion.
//this corrrection motion is supposed to be hidden from the system as much as possible.
//This method should be called by plan_buffer_line() for all the none backlash motions, 
//so that all the motion direction changes and previos positions of the machine are tracked. 
bool backlash_Compensate_befor_target(float* target, plan_line_data_t *pl_data){
    // Initialize planner data struct for backlash motion blocks.
    plan_line_data_t  plan_data_backlash;
    plan_line_data_t* pl_data_backlash = &plan_data_backlash;
    memcpy(pl_data_backlash, pl_data, sizeof(plan_line_data_t)); 

    bool backlash_comp_needed = false;

    //the backlash correction is hidden from any future GRBL or gcode calculations, 
    //in effect it should look like a hardware correction which is not visible to software. 
    pl_data_backlash->motion.backlashMotion = 1;

    if(pl_data_backlash->motion.systemMotion == 1){
        log_debug("BS_CHK DBUG: Backlash correction for a systemMotion!!");
    }
    //log_debug("BS_CHK DBUG: pl_data= "<< pl_data->motion.backlashMotion << "vs pl_data_backlash= "<< pl_data_backlash->motion.backlashMotion << " " );

    float backlash_comp_target[config->_axes->_numberAxis] ={0.0}; //comensated position to move based on mechine backlash before starting to move to target
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        if(target[axis] > backlash_data[axis].prev_target){ //Current move positive
            if(backlash_data[axis].prev_direction == MotionDirection::Negative ){ //Previous move negative, so we need to comp backlash 
                backlash_data[axis].backlash_enable = true;
                if( config->_axes->_axis[axis]->_backlash > 0 ){
                    backlash_comp_needed = true;
                }
            }
            else
                backlash_data[axis].backlash_enable = false;

            backlash_data[axis].prev_direction = MotionDirection::Positive;
        }
        else if(target[axis] < backlash_data[axis].prev_target){ //Current move negative
            if(backlash_data[axis].prev_direction == MotionDirection::Positive  ){
                backlash_data[axis].backlash_enable = true;
                if(config->_axes->_axis[axis]->_backlash > 0 ){
                    backlash_comp_needed = true;
                }
            }
            else
                backlash_data[axis].backlash_enable = false;

            backlash_data[axis].prev_direction = MotionDirection::Negative;
        }
        else
            backlash_data[axis].backlash_enable = false;

        if(backlash_data[axis].backlash_enable){
            if(backlash_data[axis].prev_direction == MotionDirection::Positive)
                backlash_comp_target[axis]  = backlash_data[axis].prev_target + config->_axes->_axis[axis]->_backlash;
            else
                backlash_comp_target[axis]  = backlash_data[axis].prev_target - config->_axes->_axis[axis]->_backlash;
        }
        else
            backlash_comp_target[axis]  = backlash_data[axis].prev_target ;

        backlash_data[axis].prev_target = target[axis];
    }

    if(backlash_comp_needed){
        log_debug( "BS_COMP (" << backlash_comp_target[0] << " ," << backlash_comp_target[1] << " ," << backlash_comp_target[2]<< " )!!");
        plan_buffer_line(backlash_comp_target, pl_data_backlash);
    }
    return true;
}

void backlash_Reset_after_probe() {
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        //Assume the probing motion stopped by system and this method gett called before any further motions
        //no chnage to the prev_direction here .  
        backlash_data[axis].prev_target      = steps_to_mpos( motor_steps[axis], axis );
    }
    log_debug("BKSL Initi to (" << backlash_data[0].prev_target << ", " <<  backlash_data[1].prev_target <<  ", " << backlash_data[2].prev_target << ") after Probe .." );
}

//This method should be called atleast by the last homing motion, so that it can keep trck of direction chnages.
//Assuming approach is approaching the limit switch and if homing is configured for the given axis, 
//this method will initialize the baclash setting properly.
void backlash_Reset_for_homing(bool approach, uint8_t homing_mask) {
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        if(config->_axes->_axis[axis]->_homing != nullptr ){
            if (bitnum_is_true(homing_mask, axis)) {
                float t_pos = steps_to_mpos( motor_steps[axis], axis );
                if(t_pos != backlash_data[axis].prev_target){
                    if(approach){
                        backlash_data[axis].prev_direction   = !config->_axes->_axis[axis]->_homing->_positiveDirection ? MotionDirection::Negative : MotionDirection::Positive;
                    }
                    else {
                        backlash_data[axis].prev_direction   =  !config->_axes->_axis[axis]->_homing->_positiveDirection ? MotionDirection::Positive : MotionDirection::Negative ;
                    }
                    backlash_data[axis].prev_target    = t_pos;
                }
                log_debug("BKSL Init axis " << axis << "  to [" << backlash_data[axis].prev_target << ", " << static_cast<int8_t>(backlash_data[axis].prev_direction)  << "] on approaching " << approach << "  for homing " );
            }
        }
    }
}

//When ever the mechine origin is reset (homing) bcklash also need to reset
//Defunct
void backlash_Reset() {
    log_debug( "Reset Backlash..!!");
    for (int i = 0; i < config->_axes->_numberAxis; i++) {
        backlash_data[i].prev_direction     = MotionDirection::Nutral; 
        backlash_data[i].prev_target        = 0.000;
    }
}

//Defunct
void backlash_Reset_after_homecycle() {
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        if(config->_axes->_axis[axis]->_homing != nullptr ){
            if(motor_steps[axis]  > config->_axes->_axis[axis]->_homing->_mpos)
                backlash_data[axis].prev_direction   =  MotionDirection::Positive;
            else if(motor_steps[axis]  < config->_axes->_axis[axis]->_homing->_mpos)
                backlash_data[axis].prev_direction   =  MotionDirection::Negative;
            else
                backlash_data[axis].prev_direction   =  MotionDirection::Nutral;
            backlash_data[axis].prev_target      = steps_to_mpos( motor_steps[axis], axis );
        }
        log_debug("BKSL Init to ("<< backlash_data[0].prev_target << ", " << backlash_data[1].prev_target << ", " << backlash_data[2].prev_target << ") after homing!! " );
    }
}

//Defunct
void backlash_Reset_systemMotion(int32_t* sys_pos) {
    log_debug( "Reset Backlash for homing!!");
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        backlash_data[axis].prev_direction     = (sys_pos[axis]> 0 ) ? MotionDirection::Positive : MotionDirection::Negative;
        backlash_data[axis].prev_target        = steps_to_mpos(sys_pos[axis], axis);
    }
    log_debug("BKSL Sys init (" << sys_pos[0] <<", " << sys_pos[1] << ", " << sys_pos[2] << ") . ");
}
