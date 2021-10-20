#include <cmath>

// Figure out how to put these into yaml.
#define LEFT_AXIS 0
#define RIGHT_AXIS 1

#define LEFT_ANCHOR_X (-534 / 2)
#define RIGHT_ANCHOR_X (534 / 2)
#define LEFT_ANCHOR_Y 250
#define RIGHT_ANCHOR_Y 250

// Constant (after machine_init) persistent state.
static float zero_left;
static float zero_right;

// Dynamic persistent state.
static float last_left;
static float last_right;
static float last_z;

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

void lengths_to_xy(float left_length, float right_length, float& x, float& y) {
    float distance  = RIGHT_ANCHOR_X - LEFT_ANCHOR_X;
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
    x = LEFT_ANCHOR_X + a;
    y = LEFT_ANCHOR_Y + h;
}

void xy_to_lengths(float x, float y, float& left_length, float& right_length) {
    // We just need to compute the respective hypotenuse of each triangle.

    float left_dy = LEFT_ANCHOR_Y - y;
    float left_dx = LEFT_ANCHOR_X - x;
    left_length   = sqrt(left_dx * left_dx + left_dy * left_dy);

    float right_dy = RIGHT_ANCHOR_Y - y;
    float right_dx = RIGHT_ANCHOR_X - x;
    right_length   = sqrt(right_dx * right_dx + right_dy * right_dy);
}

/*
This function is used as a one time setup for your machine.
*/
void machine_init() {
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

/*
  limitsCheckTravel() is called to check soft limits
  It returns true if the motion is outside the limit values
*/
bool limitsCheckTravel() {
    return false;
}

/*
  user_defined_homing(uint8_t cycle_mask) is called at the begining of the normal Grbl_ESP32 homing
  sequence.  If user_defined_homing(uint8_t cycle_mask) returns false, the rest of normal Grbl_ESP32
  homing is skipped if it returns false, other normal homing continues.  For
  example, if you need to manually prep the machine for homing, you could implement
  user_defined_homing(uint8_t cycle_mask) to wait for some button to be pressed, then return true.
*/
bool user_defined_homing(uint8_t cycle_mask) {
    // True = done with homing, false = continue with normal Grbl_ESP32 homing
    // We return true here to disable homing, since it isn't implemented here.
    // If you implement homing, please set it to false.
    return true;
}

#ifdef USE_KINEMATICS
/*
  cartesian_to_motors() converts from cartesian coordinates to motor space.

  All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

  Parameters:
    target = an N_AXIS array of target positions (where the move is supposed to go)
    pl_data = planner data (see the definition of this type to see what it is)
    position = an N_AXIS array of where the machine is starting from for this move
*/
bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
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
        segment_count = ceil(dist / SEGMENT_LENGTH);  // determine the number of segments we need ... round up so there is at least 1
    }

    if (segment_count == 0 && target[Z_AXIS] != position[Z_AXIS]) {
        // We are moving vertically.
        last_z = target[Z_AXIS];

        // Note that the left motor runs backward.
        float cables[N_AXIS] = { 0 - (last_left - zero_left), 0 + (last_right - zero_right), last_z };

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
        float cables[N_AXIS] = { 0 - (last_left - zero_left), 0 + (last_right - zero_right), seg_z };
        if (!mc_move_motors(cables, pl_data)) {
            return false;
        }
    }

    // TO DO don't need a feedrate for rapids
    return true;
}
#endif // end USE_KINEMATICS

/*
  kinematics_pre_homing() is called before normal homing
  You can use it to do special homing or just to set stuff up

  cycle_mask is a bit mask of the axes being homed this time.
*/
bool kinematics_pre_homing(uint8_t cycle_mask) {
    // Homing is not implemented.
    return false;
}

/*
  kinematics_post_homing() is called at the end of normal homing
*/
void kinematics_post_homing() {
    // Homing is not implemented.
}

#ifdef USE_FWD_KINEMATICS
/*
  The status command uses motors_to_cartesian() to convert
  your motor positions to cartesian X,Y,Z... coordinates.

  Convert the N_AXIS array of motor positions to cartesian in your code.
*/
void motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
    // The motors start at zero, but effectively at zero_left, so we need to correct for the computation.
    // Note that the left motor runs backward.
    float absolute_x, absolute_y;
    lengths_to_xy((0 - motors[LEFT_AXIS]) + zero_left, (0 + motors[RIGHT_AXIS]) + zero_right, absolute_x, absolute_y);

    // Producing these relative coordinates.
    cartesian[X_AXIS] = absolute_x;
    cartesian[Y_AXIS] = absolute_y;
    cartesian[Z_AXIS] = motors[Z_AXIS];

    // Now we have a number that if fed back into the system should produce the same value.
}
#endif // end USE_FWD_KINEMATICS

/*
  user_tool_change() is called when tool change gcode is received,
  to perform appropriate actions for your machine.
*/
void user_tool_change(uint8_t new_tool) {
    // Tool change is not implemented.
}

/*
  options.  user_defined_macro() is called with the button number to
  perform whatever actions you choose.
*/
void user_defined_macro(uint8_t index) {
    // User defined macros are not implemented.
}

/*
  user_m30() is called when an M30 gcode signals the end of a gcode file.
*/
void user_m30() {
    // There is no special support for M30 implemented.
}

// If you add any additional functions specific to your machine that
// require calls from common code, guard their calls in the common code with
// #ifdef USE_WHATEVER and add function prototypes (also guarded) to grbl.h
