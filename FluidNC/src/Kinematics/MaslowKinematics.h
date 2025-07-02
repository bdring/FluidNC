// Copyright (c) 2024 - Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    MaslowKinematics.h

    This implements Maslow CNC Kinematics for a cable-driven router system.
    
    The Maslow CNC has four anchor points (TL, TR, BL, BR) connected by cables/belts
    to a router sled. The kinematics transforms X,Y,Z coordinates into the four
    belt lengths needed to position the sled.
    
    This replaces the custom coordinate transformation logic previously handled
    in Maslow.cpp, allowing FluidNC to handle acceleration planning and feed rate
    limiting on a per-belt basis.
*/

#include "Kinematics.h"

namespace Kinematics {
    class MaslowKinematics : public KinematicSystem {
    public:
        MaslowKinematics() = default;

        MaslowKinematics(const MaslowKinematics&) = delete;
        MaslowKinematics(MaslowKinematics&&)      = delete;
        MaslowKinematics& operator=(const MaslowKinematics&) = delete;
        MaslowKinematics& operator=(MaslowKinematics&&) = delete;

        // Kinematic Interface
        void init() override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        void transform_cartesian_to_motors(float* motors, float* cartesian) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        // Configuration handlers:
        void validate() override {}
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "MaslowKinematics"; }

        ~MaslowKinematics();

        // Public access to compute functions for calibration system
        float computeTL(float x, float y, float z);
        float computeTR(float x, float y, float z);  
        float computeBL(float x, float y, float z);
        float computeBR(float x, float y, float z);
        
        // Getters for parameters used by calibration system
        float getTlX() const { return _tlX; }
        float getTlY() const { return _tlY; }
        float getTlZ() const { return _tlZ; }
        float getTrX() const { return _trX; }
        float getTrY() const { return _trY; }
        float getTrZ() const { return _trZ; }
        float getBlX() const { return _blX; }
        float getBlY() const { return _blY; }
        float getBlZ() const { return _blZ; }
        float getBrX() const { return _brX; }
        float getBrY() const { return _brY; }
        float getBrZ() const { return _brZ; }
        float getBeltEndExtension() const { return _beltEndExtension; }
        float getArmLength() const { return _armLength; }
        float getCenterX() const { return _centerX; }
        float getCenterY() const { return _centerY; }

        // Forward kinematics methods for position synchronization
        bool computeXYfromBeltLengths(float tlLength, float trLength, float& x, float& y) const;
        float measurementToXYPlane(float measurement, float zHeight) const;

    private:
        // Anchor point coordinates (in mm)
        float _tlX = -27.6f;   // Top left X
        float _tlY = 2064.9f;  // Top left Y  
        float _tlZ = 100.0f;   // Top left Z
        
        float _trX = 2924.3f;  // Top right X
        float _trY = 2066.5f;  // Top right Y
        float _trZ = 56.0f;    // Top right Z
        
        float _blX = 0.0f;     // Bottom left X
        float _blY = 0.0f;     // Bottom left Y
        float _blZ = 34.0f;    // Bottom left Z
        
        float _brX = 2953.2f;  // Bottom right X
        float _brY = 0.0f;     // Bottom right Y
        float _brZ = 78.0f;    // Bottom right Z

        // Belt and arm parameters (in mm)
        float _beltEndExtension = 30.0f;   // Belt end extension
        float _armLength = 123.4f;         // Arm length
        
        // Center offset for coordinate system transformation
        float _centerX = 0.0f;  // Will be calculated from frame dimensions
        float _centerY = 0.0f;  // Will be calculated from frame dimensions
        
        // Initialize center coordinates
        void calculateCenter();
    };
    
    // Global accessor function to get the current MaslowKinematics instance
    MaslowKinematics* getMaslowKinematics();
}  //  namespace Kinematics