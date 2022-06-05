#include "WallPlotter.h"

#include "../Machine/MachineConfig.h"

#include <cmath>

    // Kinematic equations
    //
    //               M (motor separation)
    //          V-----------------------------V
    // left motor                             right motor
    //          +--------------+--------------+
    //           \      X1     :     X2      /
    //            \            :            /
    //             \           :           /
    //              \          :Y         /
    //             H1\         :         / H2
    //                \        :        /
    //                 \       :       /
    //                  \      :      /
    //                   \     :     /
    //                    \    :    /
    //                     \   :   /
    //                      \  :  /
    //                       \ : /
    //                        \:/
    //                         + Pen (desired cartesian origin 0,0 at startup, no homing switches)
    //
    //  Need to make sure the pen is manually located to "home" per your .yaml file to 
    //  synchronize kinematics with cartesion at startup
    //  
    // Derivation for cartesian given hypotenuses:
    //  looking to solve for X1 and Y given H1, H2, M
    //  X1 does not need to be equal to X2 (from .yaml file) 
    //  pen starting origin should be below and to right of left motor
    //  motor separation (M) is abs(x1)+abs(x2)
    //  Two hypotenuses are H1 and H2
    //        H1^2=X1^2+Y^2
    //        H2^2=X2^2+Y^2
    //    subtract two above equations
    //  H1^2-H2^2 = X1^2-X2^2
    //    substitute in X2=M-X1 for X2
    //  H1^2-H2^2 = X1^2-(M-X1)^2
    //  H1^2-H2^2 = X1^2-(M^2-2*X1*M+X1^2)
    //  (H1^2-H2^2+M^2)/(2*M) = X1
    //  Y is solved via pythagorean
    //  translate to origin per .yaml
    //
    // Derivation for hypotenuses given cartesion:
    //   use pythagorean

namespace Kinematics {
    void WallPlotter::group(Configuration::HandlerBase& handler) {
        handler.item("left_axis", _left_axis);
        handler.item("left_anchor_x", _left_anchor_x);
        handler.item("left_anchor_y", _left_anchor_y);

        handler.item("right_axis", _right_axis);
        handler.item("right_anchor_x", _right_anchor_x);
        handler.item("right_anchor_y", _right_anchor_y);

        handler.item("segment_length", _segment_length);
    }

    void WallPlotter::init() {
        log_info("Kinematic system: " << name());

        // We assume the machine starts at cartesian (0, 0, 0).
        // The motors assume they start from (0, 0, 0).
        // So we need to derive the zero lengths to satisfy the kinematic equations.
        xy_to_lengths(0, 0, zero_left, zero_right);
        last_left  = zero_left;
        last_right = zero_right;
        last_z     = 0;
    }

    bool WallPlotter::kinematics_homing(AxisMask cycle_mask) {
        // Do nothing.
        return false;
    }

    void WallPlotter::kinematics_post_homing() {
        // Do nothing.
    }

    /*
      cartesian_to_motors() converts from cartesian coordinates to motor space.

      All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

      Parameters:
        target = an MAX_N_AXIS array of target positions (where the move is supposed to go)
        pl_data = planner data (see the definition of this type to see what it is)
        position = an MAX_N_AXIS array of where the machine is starting from for this move
    */
    bool WallPlotter::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        float    dx, dy, dz;        // distances in each cartesian axis
        uint32_t segment_count;     // number of segments the move will be broken in to.
        dx = target[X_AXIS] - position[X_AXIS];
        dy = target[Y_AXIS] - position[Y_AXIS];
        dz = target[Z_AXIS] - position[Z_AXIS];

        // calculate the total X,Y axis move distance
        // Z axis is the same in both coord systems, so it does not undergo conversion
        float dist = vector_distance(target, position, Y_AXIS);
        // Segment our G1 and G0 moves based on yaml file. If we choose a small enough _segment_length we can hide the nonlinearity
        segment_count = dist / _segment_length;
        if (segment_count < 1) { // Make sure there is at least one segment, even if there is no movement
            // We need to do this to make sure other things like S and M codes get updated properly by
            // the planner even if there is no movement??
            segment_count = 1;   
        }
        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            float seg_x = position[X_AXIS] + (dx / segment_count * segment);
            float seg_y = position[Y_AXIS] + (dy / segment_count * segment);
            float seg_z = position[Z_AXIS] + (dz / segment_count * segment);

            float seg_left, seg_right;
            xy_to_lengths(seg_x, seg_y, seg_left, seg_right);
            // TODO: Need to adjust cartesian feedrate to motor/plotter space, just leave them alone for now

#ifdef USE_CHECKED_KINEMATICS
            // Check the inverse computation.
            float cx, cy;
            // These are absolute.
            lengths_to_xy(seg_left, seg_right, cx, cy);

            if (abs(seg_x - cx) > 0.1 || abs(seg_y - cy) > 0.1) {
                // FIX: Produce an alarm state?
            }
#endif  // end USE_CHECKED_KINEMATICS

            // mc_move_motors() returns false if a jog is cancelled.
            // In that case we stop sending segments to the planner.
            last_left  = seg_left;
            last_right = seg_right;
            last_z     = seg_z;
            // Note that the left motor runs backward.
            // TODO: It might be better to adjust motor direction in .yaml file by inverting direction pin??
            float cables[MAX_N_AXIS] = { 0 - (last_left - zero_left), 0 + (last_right - zero_right), seg_z };
            if (!mc_move_motors(cables, pl_data)) {
                return false;
            }
        }
        return true;
    }

    /*
      The status command uses motors_to_cartesian() to convert
      your motor positions to cartesian X,Y,Z... coordinates.

      Convert the MAX_N_AXIS array of motor positions to cartesian in your code.
    */
    void WallPlotter::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // The motors start at zero, but effectively at zero_left, so we need to correct for the computation.
        // Note that the left motor runs backward.
        // TODO: It might be better to adjust motor direction in .yaml file by inverting direction pin??
        float absolute_x, absolute_y;
        lengths_to_xy((0 - motors[_left_axis]) + zero_left, (0 + motors[_right_axis]) + zero_right, absolute_x, absolute_y);
        cartesian[X_AXIS] = absolute_x;
        cartesian[Y_AXIS] = absolute_y;
        cartesian[Z_AXIS] = motors[Z_AXIS];
        // Now we have numbers that if fed back into the system should produce the same values.
    }

    
    void WallPlotter::lengths_to_xy(float h1, float h2, float& x, float& y) {
        // calculate x y lengths from the two given hypotenuses
        // Assumes origin is to the right and below of left motor...
        // TODO Optimize equations to reduce floating point operations
        float m = abs(_left_anchor_x)+abs(_right_anchor_x); // motor separation
        x = (h1*h1-h2*h2+m*m) / (2*m); // solve for X1
        y = sqrtf(h1*h1-x*x); // using X1 and H1 solve for Y
        // Now translate X1 Y per .yaml.  Assumes cartesian origin is to the right and below of left motor...
        x = x + _left_anchor_x;
        y = _right_anchor_y - y; //  flip
        //log_info("lengths_to_xy: h1:"<< h1 << " h2:"<< h2 <<" x:"<< x << " y:" << y );
    }

    void WallPlotter::xy_to_lengths(float x, float y, float& left_length, float& right_length) {
        // Compute the hypotenuse of each triangle.
        float left_dy = _left_anchor_y - y;
        float left_dx = _left_anchor_x - x;
        left_length   = hypot_f(left_dx, left_dy);

        float right_dy = _right_anchor_y - y;
        float right_dx = _right_anchor_x - x;
        right_length   = hypot_f(right_dx, right_dy);
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<WallPlotter> registration("WallPlotter");
    }
}
