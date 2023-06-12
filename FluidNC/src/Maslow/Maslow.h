#pragma once
#include <Arduino.h>
#include "MotorUnit.h"

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
};

extern Maslow_ &Maslow;