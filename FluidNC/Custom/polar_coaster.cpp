// Copyright (c) 2019 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Implements inverse kinematics for the polar coaster machine

	Inverse kinematics determine the joint parameters required to get to a position
	in 3D space. The GCode will still command motion in 3 axes, but the coordinates could
	represent angles, etc instead of linear units.

	Unless forward kinematics are applied to the reporting, reports will be in raw joint
	values instead of the normal Cartesian positions

	How it works...

	If you tell it to go to X10 Y10 Z10 in Cartesian space, for example, the equations
	will convert those values to the required joint values. In the case of a polar machine, X represents the radius,
	Y represents the polar degrees and Z would be unchanged.

	In most cases, a straight line in Cartesian space could cause a curve in the new system.
	To fix this, the line is broken into very small segments and each segment is converted
	to the new space. While each segment is also distorted, the amount is so small it cannot be seen.

	This segmentation is how normal arcs are drawn.

	Feed Rate

	Feed rate is given in steps/time. Due to the new coordinate units and non linearity issues, the
	feed rate may need to be adjusted. The ratio of the step distances in the original coordinate system
	determined and applied to the feed rate.

	TODO:
		Add y offset, for completeness
		Add ZERO_NON_HOMED_AXES option


*/

#include "../src/MotionControl.h"  // kinematics_*

// This file is enabled by defining CUSTOM_CODE_FILENAME "polar_coaster.cpp"
// in Machines/polar_coaster.h, thus causing this file to be included
// from ../custom_code.cpp

void  calc_polar(float* target_xyz, float* polar, float last_angle);
float abs_angle(float ang);

static float last_angle  = 0;
static float last_radius = 0;

// this get called before homing
// return false to complete normal home
// return true to exit normal homing
bool kinematics_pre_homing(uint8_t cycle_mask) {
    return false;  // finish normal homing cycle
}

void kinematics_post_homing() {
    // sync the X axis (do not need sync but make it for the fail safe)
    last_radius = motor_steps[X_AXIS];
    // reset the internal angle value
    last_angle = 0;
}

/*
 Apply inverse kinematics for a polar system

 float target: 					The desired target location in machine space
 plan_line_data_t *pl_data:		Plan information like feed rate, etc
 float *position:				The previous "from" location of the move

 Note: It is assumed only the radius axis (X) is homed and only X and Z have offsets


*/

bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
    float    dx, dy, dz;          // distances in each cartesian axis
    float    p_dx, p_dy, p_dz;    // distances in each polar axis
    float    dist, polar_dist;    // the distances in both systems...used to determine feed rate
    uint32_t segment_count;       // number of segments the move will be broken in to.
    float    seg_target[N_AXIS];  // The target of the current segment
    float    polar[N_AXIS];       // target location in polar coordinates
    float    x_offset = gc_state.coord_system[X_AXIS] + gc_state.coord_offset[X_AXIS];  // offset from machine coordinate system
    float    z_offset = gc_state.coord_system[Z_AXIS] + gc_state.coord_offset[Z_AXIS];  // offset from machine coordinate system
    //info_serial("Position: %4.2f %4.2f %4.2f", position[X_AXIS] - x_offset, position[Y_AXIS], position[Z_AXIS]);
    //info_serial("Target: %4.2f %4.2f %4.2f", target[X_AXIS] - x_offset, target[Y_AXIS], target[Z_AXIS]);
    // calculate cartesian move distance for each axis
    dx = target[X_AXIS] - position[X_AXIS];
    dy = target[Y_AXIS] - position[Y_AXIS];
    dz = target[Z_AXIS] - position[Z_AXIS];
    // calculate the total X,Y axis move distance
    // Z axis is the same in both coord systems, so it is ignored
    dist = sqrt((dx * dx) + (dy * dy) + (dz * dz));
    if (pl_data->motion.rapidMotion) {
        segment_count = 1;  // rapid G0 motion is not used to draw, so skip the segmentation
    } else {
        segment_count = ceil(dist / SEGMENT_LENGTH);  // determine the number of segments we need	... round up so there is at least 1
    }
    dist /= segment_count;  // segment distance
    for (uint32_t segment = 1; segment <= segment_count; segment++) {
        // determine this segment's target
        seg_target[X_AXIS] = position[X_AXIS] + (dx / float(segment_count) * segment) - x_offset;
        seg_target[Y_AXIS] = position[Y_AXIS] + (dy / float(segment_count) * segment);
        seg_target[Z_AXIS] = position[Z_AXIS] + (dz / float(segment_count) * segment) - z_offset;
        calc_polar(seg_target, polar, last_angle);
        // begin determining new feed rate
        // calculate move distance for each axis
        p_dx                      = polar[RADIUS_AXIS] - last_radius;
        p_dy                      = polar[POLAR_AXIS] - last_angle;
        p_dz                      = dz;
        polar_dist                = sqrt((p_dx * p_dx) + (p_dy * p_dy) + (p_dz * p_dz));  // calculate the total move distance
        float polar_rate_multiply = 1.0;                                                  // fail safe rate
        if (polar_dist == 0 || dist == 0) {
            // prevent 0 feed rate and division by 0
            polar_rate_multiply = 1.0;  // default to same feed rate
        } else {
            // calc a feed rate multiplier
            polar_rate_multiply = polar_dist / dist;
            if (polar_rate_multiply < 0.5) {
                // prevent much slower speed
                polar_rate_multiply = 0.5;
            }
        }
        pl_data->feed_rate *= polar_rate_multiply;  // apply the distance ratio between coord systems
        // end determining new feed rate
        polar[RADIUS_AXIS] += x_offset;
        polar[Z_AXIS] += z_offset;

        // mc_line() returns false if a jog is cancelled.
        // In that case we stop sending segments to the planner.
        if (!mc_line(polar, pl_data)) {
            return false;
        }

        //
        last_radius = polar[RADIUS_AXIS];
        last_angle  = polar[POLAR_AXIS];
    }
    // TO DO don't need a feedrate for rapids
    return true;
}

/*
Forward kinematics converts position back to the original cartesian system. It is
typically used for reporting

For example, on a polar machine, you tell it to go to a place like X10Y10. It
converts to a radius and angle using inverse kinematics. The machine posiiton is now
in those units X14.14 (radius) and Y45 (degrees). If you want to report those units as
X10,Y10, you would use forward kinematics

position = the current machine position
converted = position with forward kinematics applied.

*/
void motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
    cartesian[X_AXIS] = cos(radians(motors[Y_AXIS])) * motors[X_AXIS] * -1;
    cartesian[Y_AXIS] = sin(radians(motors[Y_AXIS])) * motors[X_AXIS];
    cartesian[Z_AXIS] = motors[Z_AXIS];  // unchanged
}

// helper functions

/*******************************************
* Calculate polar values from Cartesian values
* 	float target_xyz:	An array of target axis positions in Cartesian (xyz) space
*		float polar: 			An array to return the polar values
*		float last_angle:	The polar angle of the "from" point.
*
*   Angle calculated is 0 to 360, but you don't want a line to go from 350 to 10. This would
*   be a long line backwards. You want it to go from 350 to 370. The same is true going the other way.
*
*   This means the angle could accumulate to very high positive or negative values over the coarse of
*   a long job.
*
*/
void calc_polar(float* target_xyz, float* polar, float last_angle) {
    float delta_ang;  // the difference from the last and next angle
    polar[RADIUS_AXIS] = hypot_f(target_xyz[X_AXIS], target_xyz[Y_AXIS]);
    if (polar[RADIUS_AXIS] == 0) {
        polar[POLAR_AXIS] = last_angle;  // don't care about angle at center
    } else {
        polar[POLAR_AXIS] = atan2(target_xyz[Y_AXIS], target_xyz[X_AXIS]) * 180.0 / M_PI;
        // no negative angles...we want the absolute angle not -90, use 270
        polar[POLAR_AXIS] = abs_angle(polar[POLAR_AXIS]);
    }
    polar[Z_AXIS] = target_xyz[Z_AXIS];  // Z is unchanged
    delta_ang     = polar[POLAR_AXIS] - abs_angle(last_angle);
    // if the delta is above 180 degrees it means we are crossing the 0 degree line
    if (fabs(delta_ang) <= 180.0)
        polar[POLAR_AXIS] = last_angle + delta_ang;
    else {
        if (delta_ang > 0.0) {
            // crossing zero counter clockwise
            polar[POLAR_AXIS] = last_angle - (360.0 - delta_ang);
        } else
            polar[POLAR_AXIS] = last_angle + delta_ang + 360.0;
    }
}

// Return a 0-360 angle ... fix above 360 and below zero
float abs_angle(float ang) {
    ang = fmod(ang, 360.0);  // 0-360 or 0 to -360
    if (ang < 0.0)
        ang = 360.0 + ang;
    return ang;
}

// Polar coaster has macro buttons, this handles those button pushes.
void user_defined_macro(uint8_t index) {
    switch (index) {
        case 0:
            WebUI::inputBuffer.push("$H\r");  // home machine
            break;
        case 1:
            WebUI::inputBuffer.push("[ESP220]/1.nc\r");  // run SD card file 1.nc
            break;
        case 2:
            WebUI::inputBuffer.push("[ESP220]/2.nc\r");  // run SD card file 2.nc
            break;
        default:
            break;
    }
}

// handle the M30 command
void user_m30() {
    WebUI::inputBuffer.push("$H\r");
}
