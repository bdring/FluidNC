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
#include "../Maslow/Maslow.h"

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
            
            if (cartesian_distance > 0) {

            // Check if this is a Z-only move by examining X,Y changes
            float xy_distance = sqrt((target[X_AXIS] - position[X_AXIS]) * (target[X_AXIS] - position[X_AXIS]) + 
                                   (target[Y_AXIS] - position[Y_AXIS]) * (target[Y_AXIS] - position[Y_AXIS]));
            bool is_z_only_move = (xy_distance < 0.001f); // Consider moves < 0.001mm as Z-only

            if (is_z_only_move) {
                // For Z-only moves: Scale feed rate by Z motor movement ratio
                // The Z motor moves directly with cartesian Z, so ratio should be 1:1,
                // but we need to account for the fact that FluidNC's motion planning
                // expects proper feed rate scaling for all motor movements
                float last_motors[n_axis];
                transform_cartesian_to_motors(last_motors, position);
                
                // For Z-only moves, only consider the Z motor distance
                float z_motor_distance = fabs(motors[4] - last_motors[4]); // Z is at index 4
                float z_cartesian_distance = fabs(target[Z_AXIS] - position[Z_AXIS]);
                
                if (z_cartesian_distance > 0) {
                    pl_data->feed_rate = pl_data->feed_rate * z_motor_distance / z_cartesian_distance;
                }
            } else {
                // For X/Y moves or combined moves: Scale feed rate by motor/cartesian ratio
                // This accounts for the fact that belt movements are longer than cartesian movements
                // and ensures the actual belt speed matches the programmed feed rate
                float last_motors[n_axis];
                transform_cartesian_to_motors(last_motors, position);
                
                // Calculate distance considering all belt motors for proper feed rate scaling
                float motor_distance = vector_distance(motors, last_motors, n_axis);
                
                pl_data->feed_rate = pl_data->feed_rate * motor_distance / cartesian_distance;
            }
            }
        }

        return mc_move_motors(motors, pl_data);
    }

    void MaslowKinematics::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        /* 
        Forward kinematics for Maslow CNC - convert belt lengths back to X,Y,Z coordinates.
        
        With ABCDZX axis mapping:
        motors[0] = A axis = Top Left belt length
        motors[1] = B axis = Top Right belt length  
        motors[2] = C axis = Bottom Left belt length
        motors[3] = D axis = Bottom Right belt length
        motors[4] = Z axis = Router position
        motors[5] = X axis = (not used)
        */
        
        // The Z coordinate is straightforward - it's just the Z motor position
        cartesian[Z_AXIS] = motors[4];  // Z from Z axis (index 4 in ABCDZX)
        
        // For X,Y coordinates, we use the TL and TR belt lengths to solve the forward kinematics
        // We need to convert the raw belt lengths to XY plane distances first
        float tlBeltLength = motors[0];  // Top Left belt length (A axis)
        float trBeltLength = motors[1];  // Top Right belt length (B axis)
        
        // Convert angled belt measurements to XY plane distances
        float tlXYDistance = measurementToXYPlane(tlBeltLength, _tlZ);
        float trXYDistance = measurementToXYPlane(trBeltLength, _trZ);
        
        // Solve for X,Y position using intersection of circles
        float x, y;
        if (computeXYfromBeltLengths(tlXYDistance, trXYDistance, x, y)) {
            cartesian[X_AXIS] = x;
            cartesian[Y_AXIS] = y;
            
            static int debug_count = 0;
            if (debug_count < 5) {
                log_info("motors_to_cartesian: TL=" << tlBeltLength << " TR=" << trBeltLength << " -> X=" << x << " Y=" << y);
                debug_count++;
            }
        } else {
            // If we can't solve the kinematics, fall back to (0,0)
            // This can happen if belt lengths are inconsistent
            cartesian[X_AXIS] = 0.0f;
            cartesian[Y_AXIS] = 0.0f;
            log_error("MaslowKinematics: Failed to compute X,Y from belt lengths, using (0,0)");
        }
        
        // Copy any additional axes directly (none expected beyond Z for now)
        for (int axis = 3; axis < n_axis && axis < MAX_N_AXIS; axis++) {
            if (axis < 3) {  // Only copy if within valid cartesian range
                cartesian[axis] = motors[axis];
            }
        }
    }

    void MaslowKinematics::transform_cartesian_to_motors(float* motors, float* cartesian) {
        // In this implementation, FluidNC axis order is ABCDZX:
        // motors[0] = A axis = Top Left belt length
        // motors[1] = B axis = Top Right belt length  
        // motors[2] = C axis = Bottom Left belt length
        // motors[3] = D axis = Bottom Right belt length
        // motors[4] = Z axis = Router position
        // motors[5] = X axis = (not used, keep as 0)

        // Extract X, Y, Z coordinates from cartesian space
        float x = cartesian[X_AXIS];  // X_AXIS = 0
        float y = cartesian[Y_AXIS];  // Y_AXIS = 1
        float z = cartesian[Z_AXIS];  // Z_AXIS = 2

        // Check if belts are ready to cut - if not, don't compute belt movements
        // This allows the Z-axis to move independently when belts are not calibrated
        if (Maslow.calibration.currentState == READY_TO_CUT) {
            // Compute belt lengths for each corner and assign to correct axis
            motors[0] = computeTL(x, y, z);  // Top Left -> A axis
            motors[1] = computeTR(x, y, z);  // Top Right -> B axis
            motors[2] = computeBL(x, y, z);  // Bottom Left -> C axis
            motors[3] = computeBR(x, y, z);  // Bottom Right -> D axis
        } else {
            // When belts are not ready, keep them at their current positions
            // This prevents the motion planner from synchronizing Z-axis with large belt movements
            motors[0] = steps_to_mpos(get_axis_motor_steps(0), 0);  // Keep TL at current position
            motors[1] = steps_to_mpos(get_axis_motor_steps(1), 1);  // Keep TR at current position  
            motors[2] = steps_to_mpos(get_axis_motor_steps(2), 2);  // Keep BL at current position
            motors[3] = steps_to_mpos(get_axis_motor_steps(3), 3);  // Keep BR at current position
        }
        
        motors[4] = z;                   // Z position -> Z axis (pass through)
        motors[5] = 0.0f;                // X axis not used

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

    // Forward kinematics - compute X,Y from belt lengths
    bool MaslowKinematics::computeXYfromBeltLengths(float tlLength, float trLength, float& x, float& y) const {
        // Find the intersection of two circles centered at TL and TR anchor points
        // with radii equal to the belt lengths
        
        double d = sqrt((_tlX - _trX) * (_tlX - _trX) + (_tlY - _trY) * (_tlY - _trY));
        if (d > tlLength + trLength || d < abs(tlLength - trLength)) {
            log_info("Unable to determine machine position from belt lengths");
            return false;
        }
        
        double a = (tlLength * tlLength - trLength * trLength + d * d) / (2 * d);
        double h = sqrt(tlLength * tlLength - a * a);
        double x0 = _tlX + a * (_trX - _tlX) / d;
        double y0 = _tlY + a * (_trY - _tlY) / d;
        double rawX = x0 + h * (_trY - _tlY) / d;
        double rawY = y0 - h * (_trX - _tlX) / d;

        // Adjust to the centered coordinates (convert from frame coordinates to centered coordinates)
        x = rawX - _centerX;
        y = rawY - _centerY;

        return true;
    }

    // Convert angled belt measurement to XY plane distance
    float MaslowKinematics::measurementToXYPlane(float measurement, float zHeight) const {
        float lengthInXY = sqrt(measurement * measurement - zHeight * zHeight);
        return lengthInXY + _beltEndExtension + _armLength; // Add belt end extension and arm length
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

    // Setter methods for calibration system to update frame parameters
    void MaslowKinematics::setFrameSize(float frameSize) {
        // Update anchor coordinates for a square frame of size frameSize x frameSize
        // Keep the same Z coordinates but adjust X,Y to form a square
        _blX = 0.0f;
        _blY = 0.0f;
        _brX = frameSize;
        _brY = 0.0f;
        _tlX = 0.0f;
        _tlY = frameSize;
        _trX = frameSize;
        _trY = frameSize;
        
        // Recalculate center coordinates
        calculateCenter();
        
        log_info("Frame size updated to: " << frameSize << " x " << frameSize);
        log_info("Anchor points updated - TL: (" << _tlX << "," << _tlY << "), TR: (" << _trX << "," << _trY << 
                "), BL: (" << _blX << "," << _blY << "), BR: (" << _brX << "," << _brY << ")");
    }

    void MaslowKinematics::updateAnchorCoordinates(float tlX, float tlY, float tlZ, 
                                                  float trX, float trY, float trZ,
                                                  float blX, float blY, float blZ,
                                                  float brX, float brY, float brZ) {
        _tlX = tlX; _tlY = tlY; _tlZ = tlZ;
        _trX = trX; _trY = trY; _trZ = trZ;
        _blX = blX; _blY = blY; _blZ = blZ;
        _brX = brX; _brY = brY; _brZ = brZ;
        
        // Recalculate center coordinates
        calculateCenter();
        
        log_info("Anchor coordinates updated manually");
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