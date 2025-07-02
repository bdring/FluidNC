// Copyright (c) 2024 - Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MaslowKinematics.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"
#include "../Machine/Homing.h"
#include "../Protocol.h"
#include "../System.h"
#include <cstring>
#include "../NutsBolts.h"
#include "../MotionControl.h"
#include <cmath>

/*
Default configuration for Maslow CNC:

kinematics:
  MaslowKinematics:
    tlX: -27.6
    tlY: 2064.9
    tlZ: 100.0
    trX: 2924.3
    trY: 2066.5
    trZ: 56.0
    blX: 0.0
    blY: 0.0
    blZ: 34.0
    brX: 2953.2
    brY: 0.0
    brZ: 78.0
    beltEndExtension: 30.0
    armLength: 123.4

This implements the cable-driven kinematics for the Maslow CNC system.
The system has 5 axes:
- A, B, C, D (belt motors: TL, TR, BL, BR mapped to motors 0-3)
- Z (cartesian Z coordinate mapped to motor 4)

*/

namespace Kinematics {
    
    // Global pointer to the current MaslowKinematics instance
    static MaslowKinematics* g_maslowKinematics = nullptr;
    
    void MaslowKinematics::init() {
        log_info("Kinematic system: " << name());
        calculateCenter();
        g_maslowKinematics = this;  // Set global pointer for access
        init_position();
    }

    void MaslowKinematics::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    void MaslowKinematics::calculateCenter() {
        // Calculate center point of the frame for coordinate system transformation
        // Find the intersection of the diagonals of the rectangle (proper geometric center)
        float A = (_trY - _blY) / (_trX - _blX);
        float B = (_brY - _tlY) / (_brX - _tlX);
        _centerX = (_brY - (B * _brX) + (A * _trX) - _trY) / (A - B);
        _centerY = A * (_centerX - _trX) + _trY;
        
        log_info("Maslow center calculated: X=" << _centerX << " Y=" << _centerY);
    }

    bool MaslowKinematics::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        auto n_axis = config->_axes->_numberAxis;
        
        // Ensure we have the expected number of axes (5: A, B, C, D, Z)
        if (n_axis < 5) {
            log_error("MaslowKinematics requires at least 5 axes");
            return false;
        }

        float motors[n_axis];
        transform_cartesian_to_motors(motors, target);

        if (!pl_data->motion.rapidMotion) {
            // Calculate vector distance of the motion in cartesian coordinates (X,Y,Z only)
            float cartesian_distance = vector_distance(target, position, 3); // Only X,Y,Z for cartesian

            // Calculate vector distance of the motion in motor coordinates (all belt motors)
            float last_motors[n_axis];
            transform_cartesian_to_motors(last_motors, position);
            
            // Calculate distance considering all belt motors for proper feed rate scaling
            float motor_distance = vector_distance(motors, last_motors, n_axis);

            // Scale the feed rate by the motor/cartesian ratio
            // This ensures that the actual belt speed matches the programmed feed rate
            if (cartesian_distance > 0) {
                pl_data->feed_rate = pl_data->feed_rate * motor_distance / cartesian_distance;
            }
        }

        return mc_move_motors(motors, pl_data);
    }

    void MaslowKinematics::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        /* 
        Forward kinematics for Maslow CNC - convert belt lengths back to X,Y,Z coordinates.
        This is complex for a cable-driven system and would require solving a system of 
        non-linear equations. 
        
        With new axis mapping:
        motors[0] = X axis = Bottom Right belt length
        motors[1] = Y axis = (not used)
        motors[2] = Z axis = Router position
        motors[3] = A axis = Top Left belt length
        motors[4] = B axis = Top Right belt length  
        motors[5] = C axis = Bottom Left belt length
        
        A full implementation would involve iterative solving of the constraint equations
        to find the X,Y position that produces the given belt lengths.
        */
        
        // For now, we'll implement a simplified approach
        // The Z coordinate is straightforward - it's just the Z motor position
        cartesian[Z_AXIS] = motors[2];
        
        // For X,Y coordinates, we need to solve the inverse kinematics
        // This is complex, so for now we'll use a placeholder that assumes
        // the system is properly calibrated and we can use the last known position
        // A full implementation would use iterative methods to solve for X,Y
        
        // Placeholder implementation - would need proper inverse kinematics
        cartesian[X_AXIS] = 0.0f;  // Would solve for actual X position
        cartesian[Y_AXIS] = 0.0f;  // Would solve for actual Y position
        
        // Copy any additional axes directly
        for (int axis = 3; axis < n_axis && axis < 3; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    void MaslowKinematics::transform_cartesian_to_motors(float* motors, float* cartesian) {
        // In this implementation, FluidNC axis order is XYZABC:
        // motors[0] = X axis = Bottom Right belt length
        // motors[1] = Y axis = (not used, keep as 0)
        // motors[2] = Z axis = Router position
        // motors[3] = A axis = Top Left belt length
        // motors[4] = B axis = Top Right belt length  
        // motors[5] = C axis = Bottom Left belt length

        // Extract X, Y, Z coordinates from cartesian space
        float x = cartesian[X_AXIS];  // X_AXIS = 0
        float y = cartesian[Y_AXIS];  // Y_AXIS = 1
        float z = cartesian[Z_AXIS];  // Z_AXIS = 2

        // Debug output for coordinate transformation
        static int debug_count = 0;
        if (debug_count < 10) {  // Only log first 10 calls to avoid spam
            log_info("MaslowKinematics transform: input X=" << x << " Y=" << y << " Z=" << z << " centerX=" << _centerX << " centerY=" << _centerY);
            debug_count++;
        }

        // Compute belt lengths for each corner and assign to correct axis
        motors[0] = computeBR(x, y, z);  // Bottom Right -> X axis
        motors[1] = 0.0f;                // Y axis not used
        motors[2] = z;                   // Z position -> Z axis (pass through)
        motors[3] = computeTL(x, y, z);  // Top Left -> A axis
        motors[4] = computeTR(x, y, z);  // Top Right -> B axis
        motors[5] = computeBL(x, y, z);  // Bottom Left -> C axis

        // Debug output for motor values
        if (debug_count <= 10) {
            log_info("MaslowKinematics motors: BR=" << motors[0] << " Y=" << motors[1] << " Z=" << motors[2] << " TL=" << motors[3] << " TR=" << motors[4] << " BL=" << motors[5]);
        }

        // Handle any additional axes beyond the 6 we know about
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 6; axis < n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    // Belt length calculation functions - moved from Maslow.cpp
    float MaslowKinematics::computeTL(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        float orig_x = x, orig_y = y;
        x = x + _centerX;
        y = y + _centerY;
        float a = _tlX - x; // X dist from corner to router center
        float b = _tlY - y; // Y dist from corner to router center
        float c = 0.0f - (z + _tlZ); // Z dist from corner to router center

        float XYlength = sqrt(a * a + b * b); // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength = XYlength - (_beltEndExtension + _armLength); // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c); // Get the angled belt length

        static int tl_debug_count = 0;
        if (tl_debug_count < 5) {
            log_info("computeTL: input(" << orig_x << "," << orig_y << "," << z << ") -> frame(" << x << "," << y << ") -> length=" << length);
            tl_debug_count++;
        }

        return length;
    }

    float MaslowKinematics::computeTR(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x = x + _centerX;
        y = y + _centerY;
        float a = _trX - x;
        float b = _trY - y;
        float c = 0.0f - (z + _trZ);
        
        float XYlength = sqrt(a * a + b * b); // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength = XYlength - (_beltEndExtension + _armLength); // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c); // Get the angled belt length

        return length;
    }

    float MaslowKinematics::computeBL(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x = x + _centerX;
        y = y + _centerY;
        float a = _blX - x; // X dist from corner to router center
        float b = _blY - y; // Y dist from corner to router center
        float c = 0.0f - (z + _blZ); // Z dist from corner to router center

        float XYlength = sqrt(a * a + b * b); // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength = XYlength - (_beltEndExtension + _armLength); // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c); // Get the angled belt length

        return length;
    }

    float MaslowKinematics::computeBR(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x = x + _centerX;
        y = y + _centerY;
        float a = _brX - x;
        float b = _brY - y;
        float c = 0.0f - (z + _brZ);

        float XYlength = sqrt(a * a + b * b); // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength = XYlength - (_beltEndExtension + _armLength); // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c); // Get the angled belt length

        return length;
    }

    bool MaslowKinematics::canHome(AxisMask axisMask) {
        // For Maslow CNC, homing is typically done by retracting all belts
        // until they reach full retraction, then calibrating the system
        return true;
    }

    void MaslowKinematics::releaseMotors(AxisMask axisMask, MotorMask motors) {
        // Release the specified motors
        // This is handled by the base motor system
    }

    bool MaslowKinematics::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        // For Maslow CNC, limits are based on the frame boundaries and belt lengths
        // This is handled by the motor system and limit switches
        return false;
    }

    void MaslowKinematics::group(Configuration::HandlerBase& handler) {
        handler.item("tlX", _tlX);
        handler.item("tlY", _tlY);
        handler.item("tlZ", _tlZ);
        handler.item("trX", _trX);
        handler.item("trY", _trY);
        handler.item("trZ", _trZ);
        handler.item("blX", _blX);
        handler.item("blY", _blY);
        handler.item("blZ", _blZ);
        handler.item("brX", _brX);
        handler.item("brY", _brY);
        handler.item("brZ", _brZ);
        handler.item("beltEndExtension", _beltEndExtension);
        handler.item("armLength", _armLength);
    }

    // Destructor - clear global pointer
    MaslowKinematics::~MaslowKinematics() {
        if (g_maslowKinematics == this) {
            g_maslowKinematics = nullptr;
        }
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<MaslowKinematics> registration("MaslowKinematics");
    }
    
    // Global accessor function to get the current MaslowKinematics instance
    MaslowKinematics* getMaslowKinematics() {
        return g_maslowKinematics;
    }
}