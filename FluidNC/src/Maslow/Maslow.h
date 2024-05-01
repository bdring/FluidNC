// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "../System.h"  // sys.*

#define TCAADDR 0x70

#define CALIBRATION_GRID_SIZE_MAX (10*10)+2

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

#define HORIZONTAL 0
#define VERTICAL 1

class Maslow_ {
private:
    Maslow_() = default;  // Make constructor private

public:
    static Maslow_& getInstance();  // Accessor for singleton instance

    Maslow_(const Maslow_&)            = delete;  // no copying
    Maslow_& operator=(const Maslow_&) = delete;

public:
    //main utility functions
    void   begin(void (*sys_rt)());
    void   home();
    void   update();
    void   blinkIPAddress();
    void   heartBeat();
    bool   updateEncoderPositions();
    void   setTargets(float xTarget, float yTarget, float zTarget, bool tl = true, bool tr = true, bool bl = true, bool br = true);
    double getTargetX();
    double getTargetY();
    double getTargetZ();
    void   recomputePID();

    //math
    void  updateCenterXY();
    float computeBL(float x, float y, float z);
    float computeBR(float x, float y, float z);
    float computeTR(float x, float y, float z);
    float computeTL(float x, float y, float z);

    //calibration functions
    void runCalibration();

    void stopMotors();

    void   retractTL();
    void   retractTR();
    void   retractBL();
    void   retractBR();
    void   retractALL();
    void   extendALL();
    void   take_slack();
    void   comply();
    void   stop();
    void   eStop(String message = "Emergency stop triggered.");
    void   panic();
    void   setSafety(bool state);
    String axis_id_to_label(int axis_id);
    bool   all_axis_homed();
    bool   allAxisExtended();
    void   safety_control();
    void   set_frame_width(double width);
    void   set_frame_height(double height);
    void   update_frame_xyz();
    bool   axis_homed[4] = { false, false, false, false };
    bool   retractingTL  = false;
    bool   retractingTR  = false;
    bool   retractingBL  = false;
    bool   retractingBR  = false;

    bool extendedTL = false;
    bool extendedTR = false;
    bool extendedBL = false;
    bool extendedBR = false;

    bool extendingALL = false;
    bool complyALL    = false;
    bool takeSlack    = false;

    bool safetyOn = true;

    //Used to override and drive the motors directly
    void TLI();
    void TRI();
    void BLI();
    void BRI();
    void TLO();
    void TRO();
    void BLO();
    void BRO();
    void handleMotorOverides();
    bool checkOverides();

    //These are the current targets set by the setTargets function used for moving the machine during normal operations
    double targetX = 0;
    double targetY = 0;
    double targetZ = 0;

    MotorUnit axisTL;
    MotorUnit axisTR;
    MotorUnit axisBL;
    MotorUnit axisBR;
    int retractCurrentThreshold = 1300;
    int calibrationCurrentThreshold = 1300;

    bool axisBLHomed;
    bool axisBRHomed;
    bool axisTRHomed;
    bool axisTLHomed;
    bool calibrationInProgress;  //Used to turn off regular movements during calibration
    bool readingFromSD = false;  //Used to turn off reading from the encoders when reading from the - i dont think we need this anymore TODO
    bool using_default_config = false;
    QWIICMUX I2CMux;

    //calibration stuff

    int frame_dimention_MIN = 400;
    int frame_dimention_MAX = 5000;

    double calibrationGrid[CALIBRATION_GRID_SIZE_MAX][2] = { 0 };
    float  calibration_grid_width_mm_X               = 2000;  // mm offset from the edge of the frame
    float  calibration_grid_height_mm_Y              = 1000;  // mm offset from the edge of the frame
    int    recomputePoints[10];                               // Stores the index of the points where we want to trigger a recompute
    int    recomputeCountIndex = 0;                           // Stores the index of the recompute point we are currently on
    int    recomputeCount      = 0;                           // Stores the number of recompute points
    double calibrationDataWaiting                    = -1;   //-1 if data is not waiting, other wise the milis since the data was last sent
    bool   error                                     = false;
    String errorMessage;
    bool   generate_calibration_grid();
    void   printCalibrationGrid();
    bool   move_with_slack(double fromX, double fromY, double toX, double toY);
    int    get_direction(double x, double y, double targetX, double targetY);
    bool   checkValidMove(double fromX, double fromY, double toX, double toY);
    bool   take_measurement_avg_with_check(int waypoint, int dir);
    bool   take_measurement(int waypoint, int dir, int run);
    bool   takeSlackFunc();
    void   test_();
    void   calibration_loop();
    void   print_calibration_data();
    void   calibrationDataRecieved();
    void   checkCalibrationData();
    void   reset_all_axis();
    bool   test = false;
    bool   orientation;
    double calibration_data[4][CALIBRATION_GRID_SIZE_MAX] = { 0 };
    int    pointCount                                 = 0;  //number of actual points in the grid,  < GRID_SIZE_MAX
    int    waypoint                                   = 0;  //The current waypoint in the calibration process
    int    calibrationGridSize                        = 9;
    // //keep track of where Maslow actually is, lower left corner is 0,0
    double x;
    double y;

    //hold
    void          hold(unsigned long time);
    unsigned long holdTimer = millis();
    bool          holding   = false;
    unsigned long holdTime  = 0;

    float tlX;
    float tlY;
    float tlZ;
    float trX;
    float trY;
    float trZ;
    float blX;
    float blY;
    float blZ;
    float brX;
    float brY;
    float brZ;

private:
    float centerX;
    float centerY;

    float _beltEndExtension = 30;  //Based on the CAD model these should add to 153.4
    float _armLength        = 123.4;

    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();

    //Stores a reference to the global system runtime function to be called when blocking operations are needed
    void (*_sys_rt)() = nullptr;

    //How hard to pull the belts when taking a measurement
    int currentThreshold;

    //Used to overide and drive motors directly...dangerous
    bool TLIOveride = false;
    bool TRIOveride = false;
    bool BLIOveride = false;
    bool BRIOveride = false;
    bool TLOOveride = false;
    bool TROOveride = false;
    bool BLOOveride = false;
    bool BROOveride = false;
    unsigned long overideTimer = millis();

    bool HeartBeatEnabled = true;

};

extern Maslow_& Maslow;
