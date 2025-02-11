// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "Calibration.h"
#include "../System.h"  // sys.*
#include "../Planner.h"
#include <nvs.h>
#include "FreeRTOS.h"
#include "semphr.h"

#define TCAADDR 0x70

#define CALIBRATION_GRID_SIZE_MAX (10*10)+2

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

#define HORIZONTAL 0
#define VERTICAL 1

#define MASLOW_TELEM_FILE "M4_telemetry.bin"

// Common Default strings - especially used by config
const std::string M = "Maslow";
// Non-volatile storage name
//const char * nvs_t = "maslow";

struct TelemetryFileHeader {
    unsigned int structureSize; // 4 bytes
    char version[10];       // 10
    // if you add to the header take bytes from this
    char _unused[64]; // 64 bytes
};

struct TelemetryData {
    unsigned long timestamp;
    // motors
    double tlCurrent;
    double trCurrent;
    double blCurrent;
    double brCurrent;
    // power
    double tlPower;
    double trPower;
    double blPower;
    double brPower;
    // speed
    double tlSpeed;
    double trSpeed;
    double blSpeed;
    double brSpeed;
     // position
    double tlPos;
    double trPos;
    double blPos;
    double brPos;

    int tlState;
    int trState;
    int blState;
    int brState;

    bool extendedTL;
    bool extendedTR;
    bool extendedBL;
    bool extendedBR;

    bool extendingALL;
    bool complyALL;
    bool takeSlack;

    bool safetyOn;
    double targetX;
    double targetY;
    double targetZ;
    double x;
    double y;
    double z;

    bool test;
    int pointCount;
    int waypoint;
    int calibrationGridSize;
    unsigned long holdTimer;
    bool holding;
    unsigned long holdTime;
    float centerX;
    float centerY;
    unsigned long lastCallToPID;
    unsigned long lastMiss;
    unsigned long lastCallToUpdate;
    unsigned long extendCallTimer;
    unsigned long complyCallTimer;
};

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

    //Save and load z-axis position, set z-stop
    void saveZPos();
    void loadZPos();
    /** Sets the 'bottom' Z position, this is a 'stop' beyond which travel cannot continue */
    void setZStop();

    //calibration functions
    void runCalibration();

    void stopMotors();

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
    bool   setupComplete();
    void   safety_control();
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

    bool setupIsComplete = false;

    Calibration calibration;

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
    void getInfo();
    bool telemetry_enabled = false;
    // TODO: probably need to use this for all fields in telemetry, but we'll try without first
    // SemaphoreHandle_t telemetry_mutex = xSemaphoreCreateMutex();
    TelemetryData get_telemetry_data();
    // turns on or off telemetry gathering
    void set_telemetry(bool enabled);
    void dump_telemetry(const char * filename);
    // writes whatever is in teh telemetry buffer to SD card
    void write_telemetry_buffer(uint8_t* buffer, size_t length);

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
    float acceptableCalibrationThreshold = 0.5;
    float extendDist = 1700;

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
    int frame_dimention_MAX = 15000;

    float  (*calibrationGrid)[2] = nullptr;
    float  calibration_grid_width_mm_X               = 2000;  // mm offset from the edge of the frame
    float  calibration_grid_height_mm_Y              = 1000;  // mm offset from the edge of the frame
    int    recomputePoints[10];                               // Stores the index of the points where we want to trigger a recompute
    int    recomputeCountIndex = 0;                           // Stores the index of the recompute point we are currently on
    int    recomputeCount      = 0;                           // Stores the number of recompute points
    double calibrationDataWaiting                    = -1;   //-1 if data is not waiting, other wise the milis since the data was last sent
    bool   error                                     = false;
    String errorMessage;
    bool   generate_calibration_grid();
    //void   printCalibrationGrid();
    bool   move_with_slack(double fromX, double fromY, double toX, double toY);
    int    get_direction(double x, double y, double targetX, double targetY);
    bool   take_measurement_avg_with_check(int waypoint, int dir);
    bool   take_measurement(float result[4], int dir, int run, int current);
    float  measurementToXYPlane(float measurement, float zHeight);
    bool   takeSlackFunc();
    bool   adjustFrameSizeToMatchFirstMeasurement();
    bool   computeXYfromLengths(double TL, double TR, float &x, float &y);
    void   test_();
    void   calibration_loop();
    void   print_calibration_data();
    void   calibrationDataRecieved();
    void   checkCalibrationData();
    void   reset_all_axis();
    bool   test = false;
    bool   orientation;
    float  **calibration_data = nullptr;
    int    pointCount                                 = 0;  //number of actual points in the grid,  < GRID_SIZE_MAX
    int    waypoint                                   = 0;  //The current waypoint in the calibration process
    int    calibrationGridSize                        = 9;
    // //keep track of where Maslow actually is
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

    float _beltEndExtension = 30;  //Based on the CAD model these should add to 153.4
    float _armLength        = 123.4;

private:
    float centerX;
    float centerY;

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
    void log_telem_hdr_csv();
    void log_telem_pt_csv(TelemetryData data);
    void allocateCalibrationMemory();
    void deallocateCalibrationMemory();
};

extern Maslow_& Maslow;

// actual task loop for gathering telemetry data (runs on utility core)
void   telemetry_loop(void* unused);
