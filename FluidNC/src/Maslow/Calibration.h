#pragma once
#include <Arduino.h>

//------------------------------------------------------
//------------------------------------------------------ State Definitions
//------------------------------------------------------

#define UNKNOWN 0
#define RETRACTING 1
#define RETRACTED 2
#define EXTENDING 3
#define EXTENDEDOUT 4 //Extended is a reserved word
#define TAKING_SLACK 5
#define CALIBRATION_IN_PROGRESS 6
#define READY_TO_CUT 7


class Calibration {
public:
    // Constructor
    Calibration();

    // Public method
    void home();
    void  updateCenterXY();

    void   runCalibration();

    bool   all_axis_homed();
    bool   allAxisExtended();
    bool   setupComplete();
    void   safety_control();
    void   update_frame_xyz();

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
    void   calibration_loop();
    void   print_calibration_data();
    void   calibrationDataRecieved();
    void   checkCalibrationData();

    void allocateCalibrationMemory();
    void deallocateCalibrationMemory();

    void comply();

    void hold(unsigned long time);

    void setSafety(bool state);
    void take_slack();

    //State machine functions
    int getCurrentState();
    bool requestStateChange(int newState);

    //Public Variables
    //hold
    unsigned long holdTimer = millis();
    bool          holding   = false;
    unsigned long holdTime  = 0;


    //Public calibration state variables. These need to be public since they are accessed externally. 
    //They probably shouldn't be.

    //Variables used by retraction
    int    retractCurrentThreshold   = 1300;
    bool axisBLHomed;
    bool axisBRHomed;
    bool axisTRHomed;
    bool axisTLHomed;

    //Variables used by extension
    float extendDist                 = 1700;

    //Variables used by calibration
    bool   orientation;
    int calibrationCurrentThreshold        = 1300;
    float acceptableCalibrationThreshold   = 0.5;
    int    calibrationGridSize             = 9;
    float  calibration_grid_width_mm_X     = 2000;  // mm offset from the edge of the frame
    float  calibration_grid_height_mm_Y    = 1000;  // mm offset from the edge of the frame
    bool  calibrationInProgress;  //Used to turn off regular movements during calibration

private:

    //State machine variables
    int currentState = UNKNOWN;

    //Variables used for retracting state
    bool   axis_homed[4]             = { false, false, false, false };
    bool   retractingTL              = false;
    bool   retractingTR              = false;
    bool   retractingBL              = false;
    bool   retractingBR              = false;

    //Variables used by extending
    bool extendedTL                  = false;
    bool extendedTR                  = false;
    bool extendedBL                  = false;
    bool extendedBR                  = false;
    bool extendingALL                = false;  //This is replaced by the state machine. Delete
    bool complyALL                   = false;
    bool setupIsComplete             = false; //This should be replaced by the state machine


    //Variables used by take slack
    bool takeSlack    = false;

    //Variables used by calibration
    float  **calibration_data              = nullptr;
    int    pointCount                      = 0;  //number of actual points in the grid,  < GRID_SIZE_MAX
    int    waypoint                        = 0;  //The current waypoint in the calibration process
    int    frame_dimention_MIN             = 400; //Is this used? This should be enforced by the user settings. TODO.
    int    frame_dimention_MAX             = 15000;
    float  (*calibrationGrid)[2]           = nullptr;
    int    recomputePoints[10];                               // Stores the index of the points where we want to trigger a recompute
    int    recomputeCountIndex = 0;                           // Stores the index of the recompute point we are currently on
    int    recomputeCount      = 0;                           // Stores the number of recompute points
    double calibrationDataWaiting          = -1;   //-1 if data is not waiting, other wise the milis since the data was last sent


    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();

    

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

    bool safetyOn = true;
    bool HeartBeatEnabled = true;


};