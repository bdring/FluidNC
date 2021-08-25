// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
	custom_code_template.cpp (copy and use your machine name)

This is a template for user-defined C++ code functions.  FluidNC can be
configured to call some optional functions. These functions have weak definitions
in the main code. If you create your own version they will be used instead

Put all of your functions in a .cpp file in the "Custom" folder.
Add this to your machine definition file
#define CUSTOM_CODE_FILENAME    "../Custom/YourFile.cpp" 

Be careful to return the correct values

===============================================================================

Below are all the current weak function

*/

#include "../src/MotionControl.h"  // kinematics_*
#include "../src/Limits.h"         // limitsCheckTravel

/*
This function is used as a one time setup for your machine.
*/
void machine_init() {}

/*
This is used to initialize a display.
*/
void display_init() {}

/*
  limitsCheckTravel() is called to check soft limits
  It returns true if the motion is outside the limit values
*/
bool limitsCheckTravel() {
    return false;
}

/*
  user_defined_homing is called at the begining of the normal FluidNC homing
  sequence.  If user_defined_homing returns false, the rest of normal FluidNC
  homing is skipped if it returns false, other normal homing continues.  For
  example, if you need to manually prep the machine for homing, you could implement
  user_defined_homing to wait for some button to be pressed, then return true.
*/
bool user_defined_homing(AxisMask cycle_mask) {
    // True = done with homing, false = continue with normal FluidNC homing
    return true;
}

/*
  Inverse Kinematics converts X,Y,Z cartesian coordinate to the steps
  on your "joint" motors.  It requires the following three functions:
*/

/*
  cartesian_to_motors() converts from cartesian coordinates to motor space.

  FluidNC processes arcs by converting them into tiny little line segments.
  Kinematics in FluidNC works the same way. Search for this function across
  FluidNC for examples. You are basically converting cartesian X,Y,Z... targets to

    target = an N_AXIS array of target positions (where the move is supposed to go)
    pl_data = planner data (see the definition of this type to see what it is)
    position = an N_AXIS array of where the machine is starting from for this move
*/
bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
    // this simply moves to the target. Replace with your kinematics.
    return mc_line(target, pl_data);
}

/*
  kinematics_pre_homing is called before normal homing
  You can use it to do special homing or just to set stuff up

  cycle_mask is a bit mask of the axes being homed this time.
*/
bool kinematics_pre_homing(AxisMask cycle_mask) {
    return false;  // finish normal homing cycle
}

/*
  kinematics_post_homing() is called at the end of normal homing
*/
void kinematics_post_homing() {}

/*
  The status command uses motors_to_cartesian() to convert
  your motor positions to cartesian X,Y,Z... coordinates.

  Convert the N_AXIS array of motor positions to cartesian in your code.
*/
void motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
    // position[X_AXIS] =
    // position[Y_AXIS] =
}

/*
  user_tool_change() is called when tool change gcode is received,
  to perform appropriate actions for your machine.
*/
void user_tool_change(uint8_t new_tool) {}

/*
  options.  user_defined_macro() is called with the button number to
  perform whatever actions you choose.
*/
void user_defined_macro(uint8_t index) {}

/*
  user_m30() is called when an M30 gcode signals the end of a gcode file.
*/
void user_m30() {}
