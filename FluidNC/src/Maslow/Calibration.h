#pragma once
#include <Arduino.h>
#include "../System.h"  // sys.*
#include "../FileStream.h"

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

    void retractALL();
    void extendALL();
    void comply();

    void hold(unsigned long time);

    void setSafety(bool state);
    void take_slack();

    //Public Variables

private:
    // Member variable
    bool   axis_homed[4] = { false, false, false, false };
    //These are used to determine which arms have already been retracted when retract all is called...this state should be handled by arms?
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

    int retractCurrentThreshold = 1300;
    int calibrationCurrentThreshold = 1300;
    float acceptableCalibrationThreshold = 0.5;
    float extendDist = 1700;

    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();

    bool setupIsComplete = false;

    bool axisBLHomed;
    bool axisBRHomed;
    bool axisTRHomed;
    bool axisTLHomed;
    bool calibrationInProgress;  //Used to turn off regular movements during calibration

    bool   orientation;
    float  **calibration_data = nullptr;
    int    pointCount                                 = 0;  //number of actual points in the grid,  < GRID_SIZE_MAX
    int    waypoint                                   = 0;  //The current waypoint in the calibration process
    int    calibrationGridSize                        = 9;

    int frame_dimention_MIN = 400;
    int frame_dimention_MAX = 15000;

    float  (*calibrationGrid)[2] = nullptr;
    float  calibration_grid_width_mm_X               = 2000;  // mm offset from the edge of the frame
    float  calibration_grid_height_mm_Y              = 1000;  // mm offset from the edge of the frame
    int    recomputePoints[10];                               // Stores the index of the points where we want to trigger a recompute
    int    recomputeCountIndex = 0;                           // Stores the index of the recompute point we are currently on
    int    recomputeCount      = 0;                           // Stores the number of recompute points
    double calibrationDataWaiting                    = -1;   //-1 if data is not waiting, other wise the milis since the data was last sent

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

    //hold
    unsigned long holdTimer = millis();
    bool          holding   = false;
    unsigned long holdTime  = 0;
};