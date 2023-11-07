#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "calibration.h"
#include "../System.h"         // sys.*

#define TCAADDR 0x70
#define CALIBRATION_GRID_SIZE  100


class Maslow_ {
  private:
    Maslow_() = default; // Make constructor private

  public:
    static Maslow_ &getInstance(); // Accessor for singleton instance

    Maslow_(const Maslow_ &) = delete; // no copying
    Maslow_ &operator=(const Maslow_ &) = delete;

  public:
    //main utility functions
    void begin(void (*sys_rt)());
    void home();
    void update();
    bool updateEncoderPositions();
    void setTargets(float xTarget, float yTarget, float zTarget);
    void recomputePID();

    //math 
    void updateCenterXY();
    void computeTensions(float x, float y);
    float computeBL(float x, float y, float z);
    float computeBR(float x, float y, float z);
    float computeTR(float x, float y, float z);
    float computeTL(float x, float y, float z);

    //calibration functions 
    void runCalibration_(); // temporary
    void runCalibration();
    void printMeasurementSet(float allLengths[][4]);
    void takeColumnOfMeasurements(float x, float measurments[][4]);
    float printMeasurementMetrics(double avg, double m1, double m2, double m3, double m4, double m5);
    void takeMeasurementAvgWithCheck(float allLengths[4]);
    float takeMeasurementAvg(float allLengths[4]);
    void takeMeasurement(float lengths[]);
    void moveWithSlack(float x, float y, bool leftBelt, bool rightBelt);
    void takeUpInternalSlack();
    void retractBR_CAL();
    void retractBL_CAL();

    void stopMotors();

    void retractTL();
    void retractTR();
    void retractBL();
    void retractBR();
    void retractALL();
    void extendALL();
    void comply();
    void stop();
    void panic();
    void setSafety(bool state);
    String axis_id_to_label(int axis_id);
    bool all_axis_homed();
    void safety_control();
    void set_frame_width(double width);
    void set_frame_height(double height);
    void update_frame_xyz();
    bool axis_homed[4] = {false, false, false, false};
    bool retractingTL = false;
    bool retractingTR = false;
    bool retractingBL = false;
    bool retractingBR = false;
    
    bool extendedTL   = false;
    bool extendedTR   = false;
    bool extendedBL   = false;
    bool extendedBR   = false;

    bool extendingALL = false;
    bool complyALL = false;

    bool safetyOn = true;
    
    
    MotorUnit axisTL;
    MotorUnit axisTR;
    MotorUnit axisBL;
    MotorUnit axisBR;
    int initialized = 0;

    bool axisBLHomed;
    bool axisBRHomed;
    bool axisTRHomed;
    bool axisTLHomed;
    bool calibrationInProgress;  //Used to turn off regular movements during calibration
    bool extendingOrRetracting;  //Used to turn off stopping the motors when extending the belts from zero
    bool readingFromSD = false;          //Used to turn off reading from the encoders when reading from the 
    bool using_default_config = false; 
    QWIICMUX I2CMux;

    //calibration stuff
    float frame_width = 3500;
    float frame_height = 2500;

    int frame_dimention_MIN = 1000;
    int frame_dimention_MAX = 5000;

    double CALIBRATION_GRID_OFFSET = 750; // distance from the corner in x and y directions
    double calibrationGrid[100][2] = {0};

    void generate_calibration_grid();
    bool move_with_slack(double fromX, double fromY, double toX, double toY);
    int get_direction(double x, double y, double targetX, double targetY);
    bool take_measurement_avg_with_check(int waypoint);
    void test_();
    void calibration_loop();
    void print_calibration_data();
    void reset_all_axis();
    bool test = false;
    bool orientation;
    double calibration_data[4][CALIBRATION_GRID_SIZE] = {0}; 
    // //keep track of where Maslow actually is, lower left corner is 0,0
    double x;
    double y;

    //hold
    void hold(unsigned long time);
    unsigned long holdTimer = millis();
    bool holding = false;
    unsigned long holdTime = 0;

  private:

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
    float centerX;
    float centerY;

    float tlTension;
    float trTension;

    float _beltEndExtension;
    float _armLength;

    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID = millis();
    unsigned long lastMiss = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer = millis();
    unsigned long complyCallTimer = millis();

    //Stores a reference to the global system runtime function to be called when blocking operations are needed
    void (*_sys_rt)() = nullptr;

    //How hard to pull the belts when taking a measurement
    int currentThreshold;
    
};

extern Maslow_ &Maslow;