// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#ifndef MotorUnit_h
#define MotorUnit_h

#include "Arduino.h"
#include "Wire.h"
#include "AS5600.h"
#include "MiniPID.h"  //https://github.com/tekdemo/MiniPID
#include "DCMotor.h"
#include "memory"
#include "SparkFun_I2C_Mux_Arduino_Library.h"

class MotorUnit {
public:
    void   begin(int forwardPin, int backwardPin, int readbackPin, int encoderAddress, int channel1, int channel2, int retractCurrentThreshold);
    void   zero();
    void   setTarget(double newTarget);
    double getTarget();
    double getPosition();
    double getCurrent();
    double getPositionError();
    void   stop();
    bool   updateEncoderPosition();
    double recomputePID();
    void   decompressBelt();
    bool   comply();
    bool   retract();
    bool   extend(double targetLength);
    bool   pull_tight(int currentThreshold);
    bool   motor_test();
    bool   test();
    void   reset();  //resetting variables here, because of non-blocking, maybe there's a better way to do this

    double getMotorCurrent();  //averaged value of the last 10 measurements
    double getBeltSpeed();
    double getMotorPower();
    void   update();
    bool   onTarget(double precision);

private:
    int     _encoderAddress;
    AS5600  encoder;
    MiniPID positionPID;  //These are the P,I,D values for the servo motors
    DCMotor motor;
    double  setpoint                           = 0.0;
    double  _mmPerRevolution                   = 43.975;  //If the amount of belt extended is too long, this number needs to be bigger
    int     _stallThreshold                    = 25;      //The number of times in a row needed to trigger a warning
    int     _stallCurrent                      = 27;      //The current threshold needed to count
    int     _stallCount                        = 0;
    int     _numPosErrors                      = 0;  //Keeps track of the number of position errors in a row to detect a stall
    double  _lastPosition                      = 0.0;
    double  _commandPWM                        = 0;  //The last PWM duty cycle sent to the motor
    double  mostRecentCumulativeEncoderReading = 0;
    double  encoderReadFailurePrintTime        = millis();
    //unsigned long lastCallGetPos = millis();

    //variables to keep track of the motor current and belt speed
    double        beltSpeed             = 0;
    unsigned long beltSpeedTimer        = millis();
    double        beltSpeedLastPosition = 0;
    double        motorCurrentBuffer[10];
    unsigned long motorCurrentTimer = millis();

    //These are used when retracting the belts for storage
    int      absoluteCurrentThreshold = 1300;
    int      incrementalThreshold     = 125;
    int      incrementalThresholdHits = 0;
    float    alpha                    = .2;
    uint16_t retract_speed            = 0;
    float    retract_baseline         = 700;

    //comply variables
    unsigned long lastCallToComply  = millis();
    unsigned long lastCallToRetract = millis();
    double        lastPosition      = getPosition();
    double        amtToMove         = 0.1;

    int beltSpeedCounter = 0;
};

#endif