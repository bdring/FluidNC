#include "ParallelDelta.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // ambiguousLimit()
#include "../Machine/Homing.h"

#include "../Protocol.h"  // protocol_execute_realtime

#include <cmath>

/*
  ==================== How it Works ====================================
  On a delta machine, Grbl axis units are in radians
  The kinematics converts the cartesian moves in gcode into
  the radians to move the arms. The Grbl motion planner never sees
  the actual cartesian values.

  To make the moves straight and smooth on a delta, the cartesian moves
  are broken into small segments where the non linearity will not be noticed.
  This is similar to how Grbl draws arcs.

  For mpos reporting, the motor position in steps is proportional to arm angles 
  in radians, which is then converted to cartesian via the forward kinematics 
  transform. Arm angle 0 means horizontal.

  Positive angles are below horizontal.

  The machine's Z zero point in the kinematics is parallel to the arm axes.
  The offset of the Z distance from the arm axes to the end effector joints 
  at arm angle zero will be printed at startup on the serial port.

  Feedrate in gcode is in the cartesian units. This must be converted to the
  angles. This is done by calculating the segment move distance and the angle 
  move distance and applying that ration to the feedrate. 

  FYI: http://forums.trossenrobotics.com/tutorials/introduction-129/delta-robot-kinematics-3276/
  Better: http://hypertriangle.com/~alex/delta-robot-tutorial/


Default configuration

kinematics:
  ParallelDelta:

  TODO 
   - Constrain the geometry values to realistic values.
   - Implement $MI for dynamixel motors.

*/

namespace Kinematics {

    // trigonometric constants to speed up calculations
    const float sqrt3  = 1.732050807;
    const float dtr    = M_PI / (float)180.0;  // degrees to radians
    const float sin120 = sqrt3 / 2.0;
    const float cos120 = -0.5;
    const float tan60  = sqrt3;
    const float sin30  = 0.5;
    const float tan30  = 1.0 / sqrt3;

    // the geometry of the delta
    float rf;  // radius of the fixed side (length of motor cranks)
    float re;  // radius of end effector side (length of linkages)
    float f;   // sized of fixed side triangel
    float e;   // size of end effector side triangle

    static float last_angle[MAX_N_AXIS]     = { 0.0 };  // A place to save the previous motor angles for distance/feed rate calcs
    static float last_cartesian[MAX_N_AXIS] = { 0.0 };  // A place to save the previous motor angles for distance/feed rate calcs

    void ParallelDelta::group(Configuration::HandlerBase& handler) {
        handler.item("crank_mm", rf, 50.0, 500.0);
        handler.item("base_triangle_mm", f, 20.0, 500.0);
        handler.item("linkage_mm", re, 20.0, 500.0);
        handler.item("end_effector_triangle_mm", e, 20.0, 500.0);
        handler.item("kinematic_segment_len_mm", _kinematic_segment_len_mm, 0.05, 20.0);  //
        handler.item("homing_mpos_radians", _homing_mpos);
        handler.item("soft_limits", _softLimits);
        handler.item("max_z_mm", _max_z, -10000.0, 0.0);  //
        handler.item("use_servos", _use_servos);
    }

    void ParallelDelta::init() {
        // print a startup message to show the kinematics are enabled. Print the offset for reference
        log_info("Kinematic system:" << name() << " soft_limits:" << _softLimits);

        auto axes   = config->_axes;
        auto n_axis = config->_axes->_numberAxis;

        // warn about axissofy limits
        for (int axis = 0; axis < n_axis; axis++) {
            if (axes->_axis[axis]->_softLimits) {
                log_warn(" All soft_limits configured in axes should be false");
                break;
            }
        }

        init_position();
    }

    void ParallelDelta::init_position() {
        float angles[MAX_N_AXIS]    = { 0.0, 0.0, 0.0 };
        float cartesian[MAX_N_AXIS] = { 0.0, 0.0, 0.0 };
        // Calculate the Z offset at the arm zero angles ...
        // Z offset is the z distance from the motor axes to the end effector axes at zero angle
        motors_to_cartesian(cartesian, angles, 3);  // Sets the cartesian values
        log_info("  Z Offset:" << cartesian[Z_AXIS]);
    }

    bool ParallelDelta::invalid_line(float* cartesian) {
        if (!_softLimits)
            return false;

        float motors[MAX_N_AXIS] = { 0.0, 0.0, 0.0 };

        if (!transform_cartesian_to_motors(motors, cartesian)) {
            limit_error();
            return true;
        }

        return false;
    }

    // TO DO. This is not supported yet. Other levels of protection will prevent "damage"
    bool ParallelDelta::invalid_arc(
        float* target, plan_line_data_t* pl_data, float* position, float center[3], float radius, size_t caxes[3], bool is_clockwise_arc) {
        return false;
    }

    // copied from Cartesian . Needs to be optimized for parallel delta.
    void ParallelDelta::constrain_jog(float* target, plan_line_data_t* pl_data, float* position) {
        // log_debug("Jog Test: from(" << position[X_AXIS] << ")"
        //                             << " to(" << target[X_AXIS] << ")");
        if (!_softLimits)
            return;

        float motors[MAX_N_AXIS] = { 0.0, 0.0, 0.0 };

        // Temp fix
        // If the target is reachable do nothing
        if (transform_cartesian_to_motors(motors, target)) {
            return;
        } else {
            log_warn("Kinematics soft limit jog rejection");
            copyAxes(target, position);
        }

        // TO DO better idea
        // loop back from the target in increments of  kinematic_segment_len_mm unitl the position is valid.
        // constrain to that target.
    }

    bool ParallelDelta::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        float dx, dy, dz;  // distances in each cartesian axis
        float motor_angles[3];

        float seg_target[3];                    // The target of the current segment
        float feed_rate  = pl_data->feed_rate;  // save original feed rate
        bool  show_error = true;                // shows error once

        bool calc_ok = true;

        if (target[Z_AXIS] > _max_z) {
            log_debug("Kinematics error. Target:" << target[Z_AXIS] << " exceeds max_z:" << _max_z);
            return false;
        }

        //log_debug("Target (" << target[0] << "," << target[1] << "," << target[2]);

        calc_ok = transform_cartesian_to_motors(last_angle, position);
        if (!calc_ok) {
            log_warn("Kinematics error. Start position error (" << position[0] << "," << position[1] << "," << position[2] << ")");
            return false;
        }

        // Check the destination to see if it is in work area
        calc_ok = transform_cartesian_to_motors(motor_angles, target);
        if (!calc_ok) {
            log_warn("Kinematics error. Target unreachable (" << target[0] << "," << target[1] << "," << target[2] << ")");
            return false;
        }

        position[X_AXIS] += gc_state.coord_offset[X_AXIS];
        position[Y_AXIS] += gc_state.coord_offset[Y_AXIS];
        position[Z_AXIS] += gc_state.coord_offset[Z_AXIS];

        // calculate cartesian move distance for each axis
        dx         = target[X_AXIS] - position[X_AXIS];
        dy         = target[Y_AXIS] - position[Y_AXIS];
        dz         = target[Z_AXIS] - position[Z_AXIS];
        float dist = sqrt((dx * dx) + (dy * dy) + (dz * dz));

        // determine the number of segments we need	... round up so there is at least 1 (except when dist is 0)
        uint32_t segment_count = ceil(dist / _kinematic_segment_len_mm);

        float segment_dist = dist / ((float)segment_count);  // distance of each segment...will be used for feedrate conversion

        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            if (sys.abort) {
                return true;
            }
            //log_debug("Segment:" << segment << " of " << segment_count);
            // determine this segment's target
            seg_target[X_AXIS] = position[X_AXIS] + (dx / float(segment_count) * segment);
            seg_target[Y_AXIS] = position[Y_AXIS] + (dy / float(segment_count) * segment);
            seg_target[Z_AXIS] = position[Z_AXIS] + (dz / float(segment_count) * segment);

            //log_debug("Segment target (" << seg_target[0] << "," << seg_target[1] << "," << seg_target[2] << ")");

            // calculate the delta motor angles
            bool calc_ok = transform_cartesian_to_motors(motor_angles, seg_target);

            if (!calc_ok) {
                if (show_error) {
                    log_error("Kinematic error motors (" << motor_angles[0] << "," << motor_angles[1] << "," << motor_angles[2] << ")");
                    show_error = false;
                }
                return false;
            }
            if (pl_data->motion.rapidMotion) {
                pl_data->feed_rate = feed_rate;
            } else {
                float delta_distance = three_axis_dist(motor_angles, last_angle);
                pl_data->feed_rate   = (feed_rate * delta_distance / segment_dist);
            }

            // mc_line() returns false if a jog is cancelled.
            // In that case we stop sending segments to the planner.
            if (!mc_move_motors(motor_angles, pl_data)) {
                return false;
            }

            // save angles for next distance calc
            // This is after mc_line() so that we do not update
            // last_angle if the segment was discarded.
            memcpy(last_angle, motor_angles, sizeof(motor_angles));
        }
        return true;
    }

    void ParallelDelta::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        //log_debug("motors_to_cartesian motors: (" << motors[0] << "," << motors[1] << "," << motors[2] << ")");
        //log_info("motors_to_cartesian rf:" << rf << " re:" << re << " f:" << f << " e:" << e);

        float t = (f - e) * tan30 / 2;

        float y1 = -(t + rf * cos(motors[0]));
        float z1 = -rf * sin(motors[0]);

        float y2 = (t + rf * cos(motors[1])) * sin30;
        float x2 = y2 * tan60;
        float z2 = -rf * sin(motors[1]);

        float y3 = (t + rf * cos(motors[2])) * sin30;
        float x3 = -y3 * tan60;
        float z3 = -rf * sin(motors[2]);

        float dnm = (y2 - y1) * x3 - (y3 - y1) * x2;

        float w1 = y1 * y1 + z1 * z1;
        float w2 = x2 * x2 + y2 * y2 + z2 * z2;
        float w3 = x3 * x3 + y3 * y3 + z3 * z3;

        // x = (a1*z + b1)/dnm
        float a1 = (z2 - z1) * (y3 - y1) - (z3 - z1) * (y2 - y1);
        float b1 = -((w2 - w1) * (y3 - y1) - (w3 - w1) * (y2 - y1)) / 2.0;

        // y = (a2*z + b2)/dnm;
        float a2 = -(z2 - z1) * x3 + (z3 - z1) * x2;
        float b2 = ((w2 - w1) * x3 - (w3 - w1) * x2) / 2.0;

        // a*z^2 + b*z + c = 0
        float a = a1 * a1 + a2 * a2 + dnm * dnm;
        float b = 2 * (a1 * b1 + a2 * (b2 - y1 * dnm) - z1 * dnm * dnm);
        float c = (b2 - y1 * dnm) * (b2 - y1 * dnm) + b1 * b1 + dnm * dnm * (z1 * z1 - re * re);

        // discriminant
        float d = b * b - (float)4.0 * a * c;
        if (d < 0) {
            log_warn("Forward Kinematics Error");
            return;
        }
        cartesian[Z_AXIS] = -(float)0.5 * (b + sqrt(d)) / a;
        cartesian[X_AXIS] = (a1 * cartesian[Z_AXIS] + b1) / dnm;
        cartesian[Y_AXIS] = (a2 * cartesian[Z_AXIS] + b2) / dnm;
    }

    bool ParallelDelta::kinematics_homing(AxisMask& axisMask) {
        // only servos use custom homing. Steppers use limit switches
        if (!_use_servos)
            false;

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;

        config->_axes->set_disable(false);

        // TODO deal with non kinematic axes above Z
        for (int axis = 0; axis < 3; axis++) {
            //set_motor_steps(axis, mpos_to_steps(axes->_axis[axis]->_homing->_mpos, axis));
            int32_t steps = mpos_to_steps(_homing_mpos, axis);
            set_motor_steps(axis, steps);
        }
        protocol_disable_steppers();
        return true;  // signal main code that this handled all homing
    }

    // helper functions, calculates angle theta1 (for YZ-pane)
    bool ParallelDelta::delta_calcAngleYZ(float x0, float y0, float z0, float& theta) {
        float y1 = -0.5 * 0.57735 * f;  // f/2 * tg 30
        y0 -= 0.5 * 0.57735 * e;        // shift center to edge
        // z = a + b*y
        float a = (x0 * x0 + y0 * y0 + z0 * z0 + rf * rf - re * re - y1 * y1) / (2 * z0);
        float b = (y1 - y0) / z0;
        // discriminant
        float d = -(a + b * y1) * (a + b * y1) + rf * (b * b * rf + rf);
        if (d < 0) {
            //log_warn("Kinematics: Target unreachable");
            return false;
        }                                                 // non-existing point
        float yj = (y1 - a * b - sqrt(d)) / (b * b + 1);  // choosing outer point
        float zj = a + b * yj;

        theta = atan(-zj / (y1 - yj)) + ((yj > y1) ? M_PI : 0.0);

        return true;
    }

    void ParallelDelta::releaseMotors(AxisMask axisMask, MotorMask motors) {}

    bool ParallelDelta::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[0] = motors[1] = motors[2] = 0;
        bool calc_ok                      = false;

        if (cartesian[Z_AXIS] > _max_z) {
            log_debug("Kinematics transform error. Target:" << cartesian[Z_AXIS] << " exceeds max_z:" << _max_z);
            return false;
        }

        calc_ok = delta_calcAngleYZ(cartesian[X_AXIS], cartesian[Y_AXIS], cartesian[Z_AXIS], motors[0]);
        if (!calc_ok) {
            return calc_ok;
        }

        calc_ok = delta_calcAngleYZ(cartesian[X_AXIS] * cos120 + cartesian[Y_AXIS] * sin120,
                                    cartesian[Y_AXIS] * cos120 - cartesian[X_AXIS] * sin120,
                                    cartesian[Z_AXIS],
                                    motors[1]);  // rotate coords to +120 deg
        if (!calc_ok) {
            return calc_ok;
        }

        calc_ok = delta_calcAngleYZ(cartesian[X_AXIS] * cos120 - cartesian[Y_AXIS] * sin120,
                                    cartesian[Y_AXIS] * cos120 + cartesian[X_AXIS] * sin120,
                                    cartesian[Z_AXIS],
                                    motors[2]);  // rotate coords to -120 deg

        return calc_ok;
    }

    // Determine the unit distance between (2) 3D points
    float ParallelDelta::three_axis_dist(float* point1, float* point2) {
        return sqrt(((point1[0] - point2[0]) * (point1[0] - point2[0])) + ((point1[1] - point2[1]) * (point1[1] - point2[1])) +
                    ((point1[2] - point2[2]) * (point1[2] - point2[2])));
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<ParallelDelta> registration("parallel_delta");
    }
}
