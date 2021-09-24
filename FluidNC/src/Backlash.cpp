
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
        backlash_data[i].prev_target        = 0.000;
    }
}

bool backlash_Compensate_befor_target(float* target, plan_line_data_t *pl_data){
    // Initialize planner data struct forbacklash motion blocks.
    plan_line_data_t  plan_data_backlash;
    plan_line_data_t* pl_data_backlash = &plan_data_backlash;
    memcpy(pl_data_backlash, pl_data, sizeof(plan_line_data_t)); 

    bool backlash_comp_needed = false;

    //pl_data_backlash->is_backlash_motion = true;  
    //the backlash correction is hidden from any future GRBL or gcode calculations, 
    //in effect it is almost like a hardware correction which is not visible to software. 
    pl_data_backlash->motion.backlashMotion = 1;

    if(pl_data_backlash->motion.systemMotion == 1){
        log_debug("BS_CHK DBUG: Backlash correction for a systemMotion!!");
    }
    //log_debug("BS_CHK DBUG: pl_data= "<< pl_data->motion.backlashMotion << "vs pl_data_backlash= "<< pl_data_backlash->motion.backlashMotion << " " );

    float backlash_comp_target[config->_axes->_numberAxis] ={0.0}; //comensated position to move based on mechine backlash before starting to move to target
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        // if(axis <3)
        //     log_debug("BS_CHK Axis: " << axis<< " TGT(" <<  target[axis]<<  "), PRV_TGT( " << backlash_data[axis].prev_target<< ")");

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
        //mc_line(backlash_comp_target, pl_data_backlash);
        plan_buffer_line(backlash_comp_target, pl_data_backlash);
        //st_prep_buffer(); //Finish backlash motion rightaway, this is needed to support systemmotions to support backlash. 
    }
    // else {
    //     log_debug("No Backlash compensation..!!");
    // }
    return true;
}

void backlash_Reset_after_probe() {
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        //Since probe ended by system previous direction didnt change
        backlash_data[axis].prev_target      = steps_to_mpos( motor_steps[axis], axis ); //sys_position[axis] / axis_settings[axis]->steps_per_mm->get();
    }
    log_debug("BKSL Initi to (" << backlash_data[0].prev_target << ", " <<  backlash_data[1].prev_target <<  ", " << backlash_data[2].prev_target << ") after Probe .." );
}

void backlash_Reset_for_homing(bool approach, uint8_t homing_mask) {
    //backlash_Reset(); //For now. But we know what exactly happened in homing so we can set proper values
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        if(config->_axes->_axis[axis]->_homing != nullptr ){
            if (bitnum_is_true(homing_mask, axis)) {
                float t_pos = steps_to_mpos( motor_steps[axis], axis ); /// axis_settings[axis]->steps_per_mm->get();
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
    //grbl_msg_sendf ( CLIENT_ALL, MsgLevel::Info, "BKSL Initi to (%3.3f, %3.3f, %3.3f) after homing!! ", backlash_data[0].prev_target, backlash_data[1].prev_target, backlash_data[2].prev_target );
}

//When ever the mechine origin is reset (homing) bcklash also need to reset
void backlash_Reset() {
    log_debug( "Reset Backlash..!!");
    for (int i = 0; i < config->_axes->_numberAxis; i++) {
        backlash_data[i].prev_direction     = MotionDirection::Nutral; 
        backlash_data[i].prev_target        = 0.000;
    }
}

void backlash_Reset_after_homecycle() {
    //backlash_Reset(); //For now. But we know what exactly happened in homing so we can set proper values
    //grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "Reset Backlash for homing!!");
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) { //config->_axes->_axis[axis]->_backlash
        if(config->_axes->_axis[axis]->_homing != nullptr ){
            if(motor_steps[axis]  > config->_axes->_axis[axis]->_homing->_mpos) //axis_settings[axis]->home_mpos->get()
                backlash_data[axis].prev_direction   =  MotionDirection::Positive;
            else if(motor_steps[axis]  < config->_axes->_axis[axis]->_homing->_mpos)
                backlash_data[axis].prev_direction   =  MotionDirection::Negative;
            else
                backlash_data[axis].prev_direction   =  MotionDirection::Nutral;
            backlash_data[axis].prev_target      = steps_to_mpos( motor_steps[axis], axis ); //sys_position[axis] / axis_settings[axis]->steps_per_mm->get();
        }
        log_debug("BKSL Init to ("<< backlash_data[0].prev_target << ", " << backlash_data[1].prev_target << ", " << backlash_data[2].prev_target << ") after homing!! " );
    }
}

void backlash_Reset_systemMotion(int32_t* sys_pos) {
    //backlash_Reset(); //For now. But we know what exactly happened in homing so we can set proper values
    log_debug( "Reset Backlash for homing!!");
    for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
        // if(sys_position[axis]  > axis_settings[axis]->home_mpos->get())
        //     backlash_data[axis].prev_direction   =  MotionDirection::Positive;
        // else if(sys_position[axis]  < axis_settings[axis]->home_mpos->get())
        //     backlash_data[axis].prev_direction   =  MotionDirection::Negative;
        // else
        //     backlash_data[axis].prev_direction   =  MotionDirection::Nutral;
        backlash_data[axis].prev_direction     = (sys_pos[axis]> 0 ) ? MotionDirection::Positive : MotionDirection::Negative;
        backlash_data[axis].prev_target        = steps_to_mpos(sys_pos[axis], axis); //sys_pos[axis] / axis_settings[axis]->steps_per_mm->get();
    }
    log_debug("BKSL Sys init (" << sys_pos[0] <<", " << sys_pos[1] << ", " << sys_pos[2] << ") . ");
}