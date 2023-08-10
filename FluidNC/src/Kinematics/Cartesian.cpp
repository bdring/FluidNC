#include "Cartesian.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {
    void Cartesian::init() {
        log_info("Kinematic system: " << name());
        init_position();
    }

    // Initialize the machine position
    void Cartesian::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    // Check that the arc does not exceed the soft limits using a fast
    // algorithm that requires no transcendental functions.
    // caxes[] depends on the plane selection via G17, G18, and G19.  caxes[0] is the first
    // circle plane axis, caxes[1] is the second circle plane axis, and caxes[2] is the
    // orthogonal plane.  So for G17 mode, caxes[] is { 0, 1, 2} for { X, Y, Z}.  G18 is {2, 0, 1} i.e. {Z, X, Y}, and G19 is {1, 2, 0} i.e. {Y, Z, X}
    bool Cartesian::invalid_arc(
        float* target, plan_line_data_t* pl_data, float* position, float center[3], float radius, size_t caxes[3], bool is_clockwise_arc) {
        pl_data->limits_checked = true;

        auto axes = config->_axes;
        // Handle the orthognal axis first to get it out of the way.
        size_t the_axis = caxes[2];
        if (axes->_axis[the_axis]->_softLimits) {
            float amin = std::min(position[the_axis], target[the_axis]);
            if (amin < limitsMinPosition(the_axis)) {
                limit_error(the_axis, amin);
                return true;
            }
            float amax = std::max(position[the_axis], target[the_axis]);
            if (amax > limitsMaxPosition(the_axis)) {
                limit_error(the_axis, amax);
                return true;
            }
        }

        bool limited[2] = { axes->_axis[caxes[0]]->_softLimits, axes->_axis[caxes[1]]->_softLimits };

        // If neither axis of the circular plane has limits enabled, skip the computation
        if (!(limited[0] || limited[1])) {
            return false;
        }

        // The origin for this calculation's coordinate system is at the center of the arc.
        // The 0 and 1 entries are for the circle plane
        // and the 2 entry is the orthogonal (linear) direction

        float s[2], e[2];  // Start and end of arc in the circle plane, relative to center

        // Depending on the arc direction, set the arc start and end points relative
        // to the arc center.  Afterwards, end is always counterclockwise relative to
        // start, thus simplifying the following decision tree.
        if (is_clockwise_arc) {
            s[0] = target[caxes[0]] - center[0];
            s[1] = target[caxes[1]] - center[1];
            e[0] = position[caxes[0]] - center[0];
            e[1] = position[caxes[1]] - center[1];
        } else {
            s[0] = position[caxes[0]] - center[0];
            s[1] = position[caxes[1]] - center[1];
            e[0] = target[caxes[0]] - center[0];
            e[1] = target[caxes[1]] - center[1];
        }

        // Axis crossings - plus and minus caxes[0] and caxes[1]
        bool p[2] = { false, false };
        bool m[2] = { false, false };

        // The following decision tree determines whether the arc crosses
        // the horizontal and vertical axes of the circular plane in the
        // positive and negative half planes.  There are ways to express
        // it in fewer lines of code by converting to alternate
        // representations like angles, but this way is computationally
        // efficient since it avoids any use of transcendental functions.
        // Every path through this decision tree is either 4 or 5 simple
        // comparisons.
        if (e[1] >= 0) {                     // End in upper half plane
            if (e[0] > 0) {                  // End in quadrant 0 - X+ Y+
                if (s[1] >= 0) {             // Start in upper half plane
                    if (s[0] > 0) {          // Start in quadrant 0 - X+ Y+
                        if (s[0] <= e[0]) {  // wraparound
                            p[0] = p[1] = m[0] = m[1] = true;
                        }
                    } else {  // Start in quadrant 1 - X- Y+
                        m[0] = m[1] = p[0] = true;
                    }
                } else {             // Start in lower half plane
                    if (s[0] > 0) {  // Start in quadrant 3 - X+ Y-
                        p[0] = true;
                    } else {  // Start in quadrant 2 - X- Y-
                        m[1] = p[0] = true;
                    }
                }
            } else {                 // End in quadrant 1 - X- Y+
                if (s[1] >= 0) {     // Start in upper half plane
                    if (s[0] > 0) {  // Start in quadrant 0 - X+ Y+
                        p[1] = true;
                    } else {                 // Start in quadrant 1 - X- Y+
                        if (s[0] <= e[0]) {  // wraparound
                            p[0] = p[1] = m[0] = m[1] = true;
                        }
                    }
                } else {             // Start in lower half plane
                    if (s[0] > 0) {  // Start in quadrant 3 - X+ Y-
                        p[0] = p[1] = true;
                    } else {  // Start in quadrant 2 - X- Y-
                        m[1] = p[0] = p[1] = true;
                    }
                }
            }
        } else {                     // e[1] < 0 - end in lower half plane
            if (e[0] > 0) {          // End in quadrant 3 - X+ Y+
                if (s[1] >= 0) {     // Start in upper half plane
                    if (s[0] > 0) {  // Start in quadrant 0 - X+ Y+
                        p[1] = m[0] = m[1] = true;
                    } else {  // Start in quadrant 1 - X- Y+
                        m[0] = m[1] = true;
                    }
                } else {                     // Start in lower half plane
                    if (s[0] > 0) {          // Start in quadrant 3 - X+ Y-
                        if (s[0] >= e[0]) {  // wraparound
                            p[0] = p[1] = m[0] = m[1] = true;
                        }
                    } else {  // Start in quadrant 2 - X- Y-
                        m[1] = true;
                    }
                }
            } else {                 // End in quadrant 2 - X- Y+
                if (s[1] >= 0) {     // Start in upper half plane
                    if (s[0] > 0) {  // Start in quadrant 0 - X+ Y+
                        p[1] = m[0] = true;
                    } else {  // Start in quadrant 1 - X- Y+
                        m[0] = true;
                    }
                } else {             // Start in lower half plane
                    if (s[0] > 0) {  // Start in quadrant 3 - X+ Y-
                        p[0] = p[1] = m[0] = true;
                    } else {                 // Start in quadrant 2 - X- Y-
                        if (s[0] >= e[0]) {  // wraparound
                            p[0] = p[1] = m[0] = m[1] = true;
                        }
                    }
                }
            }
        }
        // Now check limits based on arc endpoints and axis crossings
        for (size_t a = 0; a < 2; ++a) {
            the_axis = caxes[a];
            if (limited[a]) {
                // If we crossed the axis in the positive half plane, the
                // maximum extent along that axis is at center + radius.
                // Otherwise it is the maximum coordinate of the start and
                // end positions.  Similarly for the negative half plane
                // and the minimum extent.
                float amin = m[a] ? center[a] - radius : std::min(target[the_axis], position[the_axis]);
                if (amin < limitsMinPosition(the_axis)) {
                    limit_error(the_axis, amin);
                    return true;
                }
                float amax = p[a] ? center[a] + radius : std::max(target[the_axis], position[the_axis]);
                if (amax > limitsMaxPosition(the_axis)) {
                    limit_error(the_axis, amax);
                    return true;
                }
            }
        }
        return false;
    }

    void Cartesian::constrain_jog(float* target, plan_line_data_t* pl_data, float* position) {
        auto axes   = config->_axes;
        auto n_axis = config->_axes->_numberAxis;

        float*    current_position = get_mpos();
        MotorMask lim_pin_state    = limits_get_state();

        for (int axis = 0; axis < n_axis; axis++) {
            auto axisSetting = axes->_axis[axis];
            // If the axis is moving from the current location and soft limits are on.
            if (axisSetting->_softLimits && target[axis] != current_position[axis]) {
                // When outside the axis range, only small nudges to clear switches are allowed
                bool move_positive = target[axis] > current_position[axis];
                if ((!move_positive && (current_position[axis] < limitsMinPosition(axis))) ||
                    (move_positive && (current_position[axis] > limitsMaxPosition(axis)))) {
                    // only allow a nudge if a switch is active
                    if (bitnum_is_false(lim_pin_state, Machine::Axes::motor_bit(axis, 0)) &&
                        bitnum_is_false(lim_pin_state, Machine::Axes::motor_bit(axis, 1))) {
                        target[axis] = current_position[axis];  // cancel the move on this axis
                        log_debug("Soft limit violation on " << Machine::Axes::_names[axis]);
                        continue;
                    }
                    float jog_dist = target[axis] - current_position[axis];

                    MotorMask axisMotors = Machine::Axes::axes_to_motors(1 << axis);
                    bool      posLimited = bits_are_true(Machine::Axes::posLimitMask, axisMotors);
                    bool      negLimited = bits_are_true(Machine::Axes::negLimitMask, axisMotors);

                    // if jog is positive and only the positive switch is active, then kill the move
                    // if jog is negative and only the negative switch is active, then kill the move
                    if (posLimited != negLimited) {  // XOR, because ambiguous (both) is OK
                        if ((negLimited && (jog_dist < 0)) || (posLimited && (jog_dist > 0))) {
                            target[axis] = current_position[axis];  // cancel the move on this axis
                            log_debug("Jog into active switch blocked on " << Machine::Axes::_names[axis]);
                            continue;
                        }
                    }

                    auto nudge_max = axisSetting->_motors[0]->_pulloff;
                    if (abs(jog_dist) > nudge_max) {
                        target[axis] = (jog_dist >= 0) ? current_position[axis] + nudge_max : current_position[axis] + nudge_max;
                        log_debug("Jog amount limited when outside soft limits")
                    }
                    continue;
                }

                if (target[axis] < limitsMinPosition(axis)) {
                    target[axis] = limitsMinPosition(axis);
                } else if (target[axis] > limitsMaxPosition(axis)) {
                    target[axis] = limitsMaxPosition(axis);
                } else {
                    continue;
                }
                log_debug("Jog constrained to axis range");
            }
        }
        pl_data->limits_checked = true;
    }

    bool Cartesian::invalid_line(float* cartesian) {
        auto axes   = config->_axes;
        auto n_axis = config->_axes->_numberAxis;

        for (int axis = 0; axis < n_axis; axis++) {
            float coordinate = cartesian[axis];
            if (axes->_axis[axis]->_softLimits && (coordinate < limitsMinPosition(axis) || coordinate > limitsMaxPosition(axis))) {
                limit_error(axis, coordinate);
                return true;
            }
        }
        return false;
    }

    bool Cartesian::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        // Motor space is cartesian space, so we do no transform.
        return mc_move_motors(target, pl_data);
    }

    void Cartesian::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Motor space is cartesian space, so we do no transform.
        copyAxes(cartesian, motors);
    }

    bool Cartesian::transform_cartesian_to_motors(float* motors, float* cartesian) {
        // Motor space is cartesian space, so we do no transform.
        copyAxes(motors, cartesian);
        return true;
    }

    bool Cartesian::canHome(AxisMask axisMask) {
        if (ambiguousLimit()) {
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return false;
        }
        return true;
    }

    bool Cartesian::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
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

    void Cartesian::releaseMotors(AxisMask axisMask, MotorMask motors) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                auto paxis = axes->_axis[axis];
                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 0))) {
                    paxis->_motors[0]->unlimit();
                }
                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 1))) {
                    paxis->_motors[1]->unlimit();
                }
            }
        }
    }

    bool Cartesian::kinematics_homing(AxisMask& axisMask) {
        return false;  // kinematics does not do the homing for catesian systems
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Cartesian> registration("Cartesian");
    }
}
