#include "ParallelDelta.h"

#include "Machine/MachineConfig.h"
#include "Limit.h"  // ambiguousLimit()
#include "Machine/Homing.h"

#include "Protocol.h"  // protocol_execute_realtime

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

  Feedrate in gcode is in the cartesian units. This must be converted to the
  angles. This is done by calculating the segment move distance and the angle 
  move distance and applying that ratio to the feedrate.

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
    const float sin120 = sqrt3 / 2.0;
    const float cos120 = -0.5;
    const float tan60  = sqrt3;
    const float sin30  = 0.5;
    const float tan30  = 1.0 / sqrt3;

    void ParallelDelta::group(Configuration::HandlerBase& handler) {
        handler.item("crank_mm", rf, 50.0, 500.0);
        handler.item("base_triangle_mm", f, 20.0, 500.0);
        handler.item("linkage_mm", re, 20.0, 500.0);
        handler.item("end_effector_triangle_mm", e, 20.0, 500.0);
        handler.item("kinematic_segment_len_mm", _kinematic_segment_len_mm, 0.05, 20.0);  //
        handler.item("use_servos", _use_servos);
        handler.item("up_degrees", _up_degrees, -90, 0);
    }

    void ParallelDelta::init() {
        log_info("Kinematic system:" << name());

        auto axes = config->_axes;

        auto axis0  = axes->_axis[X_AXIS];
        auto steps0 = axis0->_stepsPerMm;
        auto accel0 = axis0->_acceleration;
        auto rate0  = axis0->_maxRate;

        for (axis_t axis = X_AXIS; axis < A_AXIS; axis++) {
            auto axisp = axes->_axis[axis];

            // Force the per-axis steps_per_mm to steps per degree
            axisp->_stepsPerMm   = steps0;
            axisp->_maxRate      = rate0;
            axisp->_acceleration = accel0;
        }

        init_position();
    }

    void ParallelDelta::init_position() {
#if 0
        // Calculate the Z offset at the arm zero angles ...
        // Z offset is the z distance from the motor axes to the end effector axes at zero angle

        auto n_axis = Axes::_numberAxis;
        float cartesian[n_axis];
        float motor_pos[n_axis];
        setArray(angles, 0.0, n_axis);
        motors_to_cartesian(cartesian, motor_pos, n_axis);  // Sets the cartesian values
        log_info("  Z Offset: " << cartesian[Z_AXIS]);
#endif
    }

    bool ParallelDelta::invalid_line(float* cartesian) {
        float motor_pos[MAX_N_AXIS] = { 0.0 };

        if (!transform_cartesian_to_motors(motor_pos, cartesian)) {
            log_info("Soft limit at " << cartesian[0] << "," << cartesian[1] << "," << cartesian[2]);
            limit_error();
            return true;
        }

        return false;
    }

    // TO DO. This is not supported yet. Other levels of protection will prevent "damage"
    bool ParallelDelta::invalid_arc(float*            target,
                                    plan_line_data_t* pl_data,
                                    float*            position,
                                    float             center[3],
                                    float             radius,
                                    axis_t            caxes[3],
                                    bool              is_clockwise_arc,
                                    uint32_t          rotations) {
        return false;
    }

    // copied from Cartesian . Needs to be optimized for parallel delta.
    void ParallelDelta::constrain_jog(float* target, plan_line_data_t* pl_data, float* position) {

        float motor_pos[MAX_N_AXIS] = { 0.0 };

        // Temp fix
        // If the target is reachable do nothing
        if (transform_cartesian_to_motors(motor_pos, target)) {
            return;
        }

        log_warn("Kinematics soft limit jog rejection");
        copyAxes(target, position);

        // TO DO better idea
        // loop back from the target in increments of  kinematic_segment_len_mm until the position is valid.
        // constrain to that target.
    }

    bool ParallelDelta::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        axis_t n_axis = Axes::_numberAxis;

        float seg_target[n_axis];              // The target of the current segment
        float feed_rate = pl_data->feed_rate;  // save original feed rate

        // Check the destination to see if it is in work area
        float motors[n_axis];
        if (!transform_cartesian_to_motors(motors, target)) {
            log_warn("Kinematics error. Target unreachable (" << target[0] << "," << target[1] << "," << target[2] << ")");
            return false;
        }

        float d[n_axis];
        copyAxes(d, target, n_axis);
        subtractAxes(d, position, n_axis);

        // determine the number of segments we need, rounding up
        // Only the xyz axes need to be considered for determining the
        // segment count, since the other axes move linearly

        uint32_t segment_count = ceil(vector_length(d, 3) / _kinematic_segment_len_mm);
        if (segment_count == 0) {
            // This can happen if the motion is entirely in other axes
            segment_count = 1;
        }

        // The all-axis segment distance is used for feedrate conversion
        float segment_dist = vector_length(d, n_axis) / segment_count;

        copyAxes(seg_target, position, n_axis);
        float delta_d[n_axis];
        copyArray(delta_d, d, n_axis);
        multiplyArray(delta_d, 1.0f / segment_count, n_axis);

        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            if (sys.abort()) {
                return true;
            }

            addAxes(seg_target, delta_d, n_axis);

            // calculate the delta motor angles
            if (!transform_cartesian_to_motors(motors, seg_target)) {
                //                if (show_error) {
                log_error("Kinematic error motors (" << motors[0] << "," << motors[1] << "," << motors[2] << ")");
                //                    show_error = false;
                //                }
                return false;
            }

            // The planner sets the feed_rate for rapids,
            if (!pl_data->motion.rapidMotion) {
                float delta_distance = vector_distance(motors, _last_motor_pos, n_axis);
                pl_data->feed_rate   = (feed_rate * delta_distance / segment_dist);
            }

            // mc_line() returns false if a jog is cancelled.
            // In that case we stop sending segments to the planner.
            if (!mc_move_motors(motors, pl_data)) {
                return false;
            }

            // save motor position for next distance calc
            // This is after mc_move_motors() so that we do not update
            // last_angle if the segment was discarded.
            copyAxes(_last_motor_pos, motors, n_axis);
        }
        return true;
    }

    bool ParallelDelta::canHome(AxisMask axisMask) {
        return Cartesian::canHome(axisMask);
    }

    void ParallelDelta::motors_to_cartesian(float* cartesian, float* motors, axis_t n_axis) {
        float radians[3];
        copyArray(radians, motors, 3);
        float scaler = pos_to_radians(1);
        multiplyArray(radians, scaler, 3);

        float t = (f - e) * tan30 / 2;
        // t is the difference between the two triangles at the midpoints

        float y1 = -(t + rf * cosf(radians[0]));
        float z1 = -rf * sinf(radians[0]);

        float y2 = (t + rf * cosf(radians[1])) * sin30;
        float x2 = y2 * tan60;
        float z2 = -rf * sinf(radians[1]);

        float y3 = (t + rf * cosf(radians[2])) * sin30;
        float x3 = -y3 * tan60;
        float z3 = -rf * sinf(radians[2]);

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

        cartesian[Z_AXIS] = (-(float)0.5 * (b + sqrtf(d)) / a);
        cartesian[X_AXIS] = (a1 * cartesian[Z_AXIS] + b1) / dnm;
        cartesian[Y_AXIS] = (a2 * cartesian[Z_AXIS] + b2) / dnm;

        axis_t axis;
        for (axis = X_AXIS; axis < A_AXIS; axis++) {
            cartesian[axis] += _mpos_offset[axis];
        }
        // Non-transformed axes
        for (; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    void ParallelDelta::motorVector(
        AxisMask axisMask, MotorMask motorMask, Machine::Homing::Phase phase, float* target, float& rate, uint32_t& settle_ms) {

        // We depend on all three arms being the same, so we get the limits,
        // rates and whatnot from only the X axis values.

        auto n_axis = Axes::_numberAxis;

        auto axes       = config->_axes;
        auto axisConfig = axes->_axis[X_AXIS];
        auto homing     = axisConfig->_homing;
        if (!homing) {
            log_error("Homing is not defined for X axis");
            return;
        }
        settle_ms    = homing->_settle_ms;
        auto pulloff = axisConfig->_motors[0]->_pulloff;

        switch (phase) {
            case Machine::Homing::Phase::PrePulloff:
                // Force the initial motor positions only on initial entry,
                // not on replans after some limits are reached
                if ((motorMask & 7) == 7) {
                    setArray(_last_motor_pos, 0, 3);
                    set_motor_pos(_last_motor_pos, 3);
                }
                setArray(target, pulloff, 3);
                rate = homing->_feedRate;
                break;

            case Machine::Homing::Phase::FastApproach:
                // Set position the first time
                // not on replans after some limits are reached
                if ((motorMask & 7) == 7) {
                    // For the initial approach we do not know where the motors are, so
                    // assume the worst case where all arms are opposite from the homed
                    // position.  That is probably too pessimistic since the arms probably
                    // cannot go quite that far, but it gives us plenty of motion range
                    // to find the limit switches without excessive overtravel if the
                    // limits are missed.

                    setArray(_last_motor_pos, 90, 3);
                    set_motor_pos(_last_motor_pos, axis_t(3));
                }
                // Modify only the motors that are still moving
                for (size_t i = 0; i < 3; i++) {
                    if (bitnum_is_true(motorMask, i)) {
                        target[i] = -90;
                    }
                }
                rate = homing->_seekRate;
                break;

            case Machine::Homing::Phase::SlowApproach:
                // The starting position is _up_degrees
                setArray(target, (_up_degrees - pulloff) * homing->_feed_scaler, 3);
                rate = homing->_feedRate;
                break;

            case Machine::Homing::Phase::Pulloff0:
            case Machine::Homing::Phase::Pulloff1:
                // The starting position is _up_degrees - pulloff
                setArray(target, _up_degrees, 3);
                rate = homing->_feedRate;
                break;

            case Machine::Homing::Phase::Pulloff2:
                rate = 0;
                break;

            default:  // None, CycleDone.
                rate = 0;
                break;
        }

        copyArray(_last_motor_pos, get_motor_pos(), n_axis);
    }

    void ParallelDelta::homing_move(AxisMask axisMask, MotorMask motorMask, Machine::Homing::Phase phase, uint32_t settling_ms) {
        if ((axisMask & 7) && (axisMask > 7)) {
            log_error("Delta axes XYZ cannot be homed in the same cycle as other axes");
            return;
        }
        // Home non-XYZ axes using the cartesian method
        if (axisMask > 7) {
            Cartesian::homing_move(axisMask, motorMask, phase, settling_ms);
            return;
        }

        releaseMotors(axisMask, motorMask);

        plan_line_data_t plan_data      = {};
        plan_data.spindle_speed         = 0;
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.spindle               = SpindleState::Disable;
        plan_data.coolant               = {};
        plan_data.line_number           = 0;
        plan_data.is_jog                = false;

        auto n_axis = Axes::_numberAxis;

        // Prime the array with the current motor positions in all axes.
        // motorVector only adjusts the delta motors and we do not want
        // to move other ones
        float motor_pos[n_axis];
        copyAxes(motor_pos, get_motor_pos());

        motorVector(axisMask, motorMask, phase, motor_pos, plan_data.feed_rate, settling_ms);

        if (plan_data.feed_rate) {
            mc_move_motors(motor_pos, &plan_data);

            copyAxes(_last_motor_pos, motor_pos, n_axis);

            protocol_send_event(&cycleStartEvent);
        }
    }

    bool ParallelDelta::kinematics_homing(AxisMask& axisMask) {
        // only servos use custom homing. Steppers use limit switches
        if (!_use_servos) {
            return false;
        }

        // For servo motors, we let the motor do the homing and then
        // set the position accordingly
        Axes::set_disable(false);

        float motor_pos[3];
        setArray(motor_pos, _up_degrees, 3);
        set_motor_pos(motor_pos, 3);

        protocol_disable_steppers();
        return true;  // signal main code that this handled all homing
    }

    // helper functions, calculates angle theta1 (for YZ-plane)
    bool ParallelDelta::delta_calcAngleYZ(float x0, float y0, float z0, float& theta) {
        float y1 = -0.5 * tan30 * f;  // f/2 * tg 30
        y0 -= 0.5 * tan30 * e;        // shift center to edge
        // z = a + b*y
        float a = (x0 * x0 + y0 * y0 + z0 * z0 + rf * rf - re * re - y1 * y1) / (2 * z0);
        float b = (y1 - y0) / z0;

        // discriminant
        float d = -(a + b * y1) * (a + b * y1) + rf * (b * b * rf + rf);
        if (d < 0) {
            log_debug("Kinematics: negative discriminant " << d);
            return false;
            // non-existing point
        }
        float yj = (y1 - a * b - sqrtf(d)) / (b * b + 1);  // choosing outer point
        float zj = a + b * yj;

        theta = radians_to_pos(atan2f(-zj, y1 - yj));  // Result is in -180..180 in steps

        return theta > (_up_degrees - 1);  // A little extra for roundoff errors
    }

    bool ParallelDelta::transform_cartesian_to_motors(float* motors, float* cartesian) {
        float xyz[3];
        copyArray(xyz, cartesian, 3);
        subtractArray(xyz, _mpos_offset, 3);

        // Copy non-transformed axes
        for (axis_t axis = A_AXIS; axis < Axes::_numberAxis; axis++) {
            motors[axis] = cartesian[axis];
        }

        if (!delta_calcAngleYZ(xyz[X_AXIS], xyz[Y_AXIS], xyz[Z_AXIS], motors[0])) {
            return false;
        }

        // Pre-calculate factors for speed
        float x_cos120 = xyz[X_AXIS] * cos120;
        float x_sin120 = xyz[X_AXIS] * sin120;
        float y_cos120 = xyz[Y_AXIS] * cos120;
        float y_sin120 = xyz[Y_AXIS] * sin120;

        // rotate coords to +120 deg
        if (!delta_calcAngleYZ(x_cos120 + y_sin120, y_cos120 - x_sin120, xyz[Z_AXIS], motors[1])) {
            return false;
        }

        // rotate coords to -120 deg
        if (!delta_calcAngleYZ(x_cos120 - y_sin120, y_cos120 + x_sin120, xyz[Z_AXIS], motors[2])) {
            return false;
        }

        return true;
    }

    void ParallelDelta::set_homed_mpos(float* mpos) {
        // In linear spaces like Cartesian and CoreXy, the origin
        // in the G53 "MPos" coordinate system can be established
        // by offsetting the motor coordinates.  That does not work
        // for Delta kinematics that requires specific arm angles
        // for a usable work envelope.  So we compute the XYZ position
        // corresponding to the homed position of the arms, then
        // set values of an offset array to translate that position
        // to the desired per-axis mpos_mm coordinates.

        // Zero out the offset array before calculating the homed XYZ position
        setArray(_mpos_offset, 0.0, 3);

        // Calculate the XYZ position resulting from the homed arm positions
        float this_mpos[3];
        motors_to_cartesian(this_mpos, _last_motor_pos, axis_t(3));

        // Calculate the offsets to translate into G53 MPos coordinates
        copyArray(_mpos_offset, mpos, 3);
        subtractArray(_mpos_offset, this_mpos, 3);

        // For any non-delta axes, use the usual method of setting
        // the motor positions for the desired coordinate offsets
        // This assumes that the non-delta axes are not transformed
        auto n_axis = Axes::_numberAxis;
        if (n_axis > A_AXIS) {
            for (axis_t axis = A_AXIS; axis < n_axis; axis++) {
                set_steps(axis, motor_pos_to_steps(mpos[axis], axis));
            }
        }
    }

    bool ParallelDelta::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
        auto axes       = config->_axes;
        auto axisConfig = axes->_axis[X_AXIS];
        auto pulloff    = axisConfig->_motors[0]->_pulloff;

        for (size_t motor = 0; motor < 3; motor++) {
            if (bitnum_is_true(limited, motor)) {
                set_motor_pos(motor, degrees_to_pos(_up_degrees - pulloff));
            }
        }

        // For Cartesian, the limit switches are associated with individual motors, since
        // an axis can have dual motors each with its own limit switch.  We clear the motors in
        // the mask whose limits have been reached.
        clear_bits(motorMask, limited);

        auto oldAxisMask = axisMask;

        // Set axisMask according to the motors that are still running.
        axisMask = Machine::Axes::motors_to_axes(motorMask);

        // Return true when an axis drops out of the mask, causing replan
        // on any remaining axes.
        return axisMask != oldAxisMask;
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<ParallelDelta> registration("parallel_delta");
    }
}
