#include "WallPlotter.h"

#include "../Machine/MachineConfig.h"

#include <cmath>

/*
Default configuration

kinematics:
  WallPlotter:
    left_axis: 0
    left_anchor_x: -267.000
    left_anchor_y: 250.000
    right_axis: 1
    right_anchor_x: 267.000
    right_anchor_y: 250.000
    segment_length: 10.000
*/

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
        //
        // TODO: Maybe we can change where the motors start, which would be simpler?
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
        float    p_dx, p_dy, p_dz;  // distances in each polar axis
        uint32_t segment_count;     // number of segments the move will be broken in to.

        // calculate cartesian move distance for each axis
        dx = target[X_AXIS] - position[X_AXIS];
        dy = target[Y_AXIS] - position[Y_AXIS];
        dz = target[Z_AXIS] - position[Z_AXIS];

        // calculate the total X,Y axis move distance
        // Z axis is the same in both coord systems, so it is ignored
        float dist = sqrt((dx * dx) + (dy * dy));
        if (pl_data->motion.rapidMotion) {
            segment_count = 1;  // rapid G0 motion is not used to draw, so skip the segmentation
        } else {
            segment_count = ceil(dist / _segment_length);  // determine the number of segments we need ... round up so there is at least 1
        }

        if (segment_count == 0 && target[Z_AXIS] != position[Z_AXIS]) {
            // We are moving vertically.
            last_z = target[Z_AXIS];

            // Note that the left motor runs backward.
            float cables[MAX_N_AXIS] = { 0 - (last_left - zero_left), 0 + (last_right - zero_right), last_z };

            if (!mc_move_motors(cables, pl_data)) {
                return false;
            }
        }

        dist /= segment_count;  // segment distance
        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            // determine this segment's absolute target
            float seg_x = position[X_AXIS] + (dx / float(segment_count) * segment);
            float seg_y = position[Y_AXIS] + (dy / float(segment_count) * segment);
            float seg_z = position[Z_AXIS] + (dz / float(segment_count) * segment);

            float seg_left, seg_right;
            // FIX: Z needs to be interpolated properly.

            xy_to_lengths(seg_x, seg_y, seg_left, seg_right);

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
            float cables[MAX_N_AXIS] = { 0 - (last_left - zero_left), 0 + (last_right - zero_right), seg_z };
            if (!mc_move_motors(cables, pl_data)) {
                return false;
            }
        }

        // TO DO don't need a feedrate for rapids
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
        float absolute_x, absolute_y;
        lengths_to_xy((0 - motors[_left_axis]) + zero_left, (0 + motors[_right_axis]) + zero_right, absolute_x, absolute_y);

        // Producing these relative coordinates.
        cartesian[X_AXIS] = absolute_x;
        cartesian[Y_AXIS] = absolute_y;
        cartesian[Z_AXIS] = motors[Z_AXIS];

        // Now we have a number that if fed back into the system should produce the same value.
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
        float h  = sqrt(left_radius2 - a2);

        // Translate to absolute coordinates.
        x = _left_anchor_x + a;
        y = _left_anchor_y + h;
    }

    void WallPlotter::xy_to_lengths(float x, float y, float& left_length, float& right_length) {
        // We just need to compute the respective hypotenuse of each triangle.

        float left_dy = _left_anchor_y - y;
        float left_dx = _left_anchor_x - x;
        left_length   = sqrt(left_dx * left_dx + left_dy * left_dy);

        float right_dy = _right_anchor_y - y;
        float right_dx = _right_anchor_x - x;
        right_length   = sqrt(right_dx * right_dx + right_dy * right_dy);
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<WallPlotter> registration("WallPlotter");
    }
}
