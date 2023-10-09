#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "calibration.h"
#include "../System.h"         // sys.*

#define TCAADDR 0x70



class Maslow_ {
  private:
    Maslow_() = default; // Make constructor private

  public:
    static Maslow_ &getInstance(); // Accessor for singleton instance

    Maslow_(const Maslow_ &) = delete; // no copying
    Maslow_ &operator=(const Maslow_ &) = delete;

  public:
    void begin(void (*sys_rt)());
    //void readEncoders();
    void home(int axis);
    void updateCenterXY();
    void recomputePID();
    void computeTensions(float x, float y);
    float computeBL(float x, float y, float z);
    float computeBR(float x, float y, float z);
    float computeTR(float x, float y, float z);
    float computeTL(float x, float y, float z);
    void setTargets(float xTarget, float yTarget, float zTarget);
    void runCalibration();
    void printMeasurements(float lengths[]);
    void lowerBeltsGoSlack();
    void printMeasurementSet(float allLengths[][4]);
    void takeColumnOfMeasurements(float x, float measurments[][4]);
    float printMeasurementMetrics(double avg, double m1, double m2, double m3, double m4, double m5);
    void takeMeasurementAvgWithCheck(float allLengths[4]);
    float takeMeasurementAvg(float allLengths[4]);
    void takeMeasurement(float lengths[]);
    void moveWithSlack(float x, float y, bool leftBelt, bool rightBelt);
    void takeUpInternalSlack();
    float computeVertical(float firstUpper, float firstLower, float secondUpper, float secondLower);
    void computeFrameDimensions(float lengthsSet1[], float lengthsSet2[], float machineDimensions[]);
    void retractBR();
    void retractBL();
    




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

    //Stores a reference to the global system runtime function to be called when blocking operations are needed
    void (*_sys_rt)() = nullptr;

    //How hard to pull the belts when taking a measurement
    int currentThreshold;
    
};

extern Maslow_ &Maslow;