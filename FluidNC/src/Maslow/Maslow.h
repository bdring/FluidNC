#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "calibration.h"
#include "../System.h"         // sys.*

class Maslow_ {
  private:
    Maslow_() = default; // Make constructor private

  public:
    static Maslow_ &getInstance(); // Accessor for singleton instance

    Maslow_(const Maslow_ &) = delete; // no copying
    Maslow_ &operator=(const Maslow_ &) = delete;

  public:
    void begin();
    void readEncoders();
    void home(int axis);
    void updateCenterXY();
    void recomputePID(int encoderNumber2Compute);
    void computeTensions(float x, float y);
    float computeBL(float x, float y, float z);
    float computeBR(float x, float y, float z);
    float computeTR(float x, float y, float z);
    float computeTL(float x, float y, float z);
    void setTargets(float xTarget, float yTarget, float zTarget);
    void runCalibration();
    void printMeasurements(float lengths[]);
    void lowerBeltsGoSlack();
    float printMeasurementMetrics(double avg, double m1, double m2, double m3, double m4, double m5);
    void takeMeasurementAvgWithCheck(float lengths[]);
    float takeMeasurementAvg(float avgLengths[]);
    void takeMeasurement(float lengths[]);
    void moveWithSlack(float x, float y);
    void takeUpInternalSlack();
    float computeVertical(float firstUpper, float firstLower, float secondUpper, float secondLower);
    void computeFrameDimensions(float lengthsSet1[], float lengthsSet2[], float machineDimensions[]);





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
};

extern Maslow_ &Maslow;