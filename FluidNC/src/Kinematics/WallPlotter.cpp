#include "WallPlotter.h"

#include "../Machine/MachineConfig.h"

#include <cmath>

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
        float    dx, dy, dz;        // segment distances in each cartesian axis
        uint32_t segment_count;     // number of segments the move will be broken in to.

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
        // Calc distance of individual segments
        dx = (target[X_AXIS] - position[X_AXIS])/segment_count; 
        dy = (target[Y_AXIS] - position[Y_AXIS])/segment_count; 
        dz = (target[Z_AXIS] - position[Z_AXIS])/segment_count;
        // Current cartesian end point of the segment        
        float seg_x = position[X_AXIS];  
        float seg_y = position[Y_AXIS];
        float seg_z = position[Z_AXIS];
        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            // calc next cartesian end point of the next segment
            seg_x += dx;
            seg_y += dy;
            seg_z += dz;
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

    /*
    Kinematic equations

    See http://paulbourke.net/geometry/circlesphere/

    First calculate the distance d between the center of the circles. d = ||P1 - P0||.

    If d > r0 + r1 then there are no solutions, the circles are separate.
    If d < |r0 - r1| then there are no solutions because one circle is contained within the other.
    If d = 0 and r0 = r1 then the circles are coincident and there are an infinite number of solutions.
    Considering the two triangles P0P2P3 and P1P2P3 we can write
    a2 + h2 = r02 and b2 + h2 = r12
    Using d = a + b we can solve for a,

    a = (r02 - r12 + d2 ) / (2 d)
    It can be readily shown that this reduces to r0 when the two circles touch at one point, ie: d = r0 ± r1

    Solve for h by substituting a into the first equation, h2 = r02 - a2

    h = sqrt(r02 - a2)
    */

    void WallPlotter::lengths_to_xy(float left_length, float right_length, float& x, float& y) {
        float distance  = _right_anchor_x - _left_anchor_x;
        float distance2 = distance * distance;

        // The lengths are the radii of the circles to intersect.
        float left_radius  = left_length;
        float left_radius2 = left_radius * left_radius;

        float right_radius  = right_length;
        float right_radius2 = right_radius * right_radius;

        // Compute a and h.
        float a  = (left_radius2 - right_radius2 + distance2) / (2 * distance);
        float a2 = a * a;
        float h  = sqrtf(left_radius2 - a2);

        // Translate to absolute coordinates.
        x = _left_anchor_x + a;
        y = _left_anchor_y - h;  // flip
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
