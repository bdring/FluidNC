#ifndef MotorUnit_h
#define MotorUnit_h

#include "Arduino.h"
#include "Wire.h"
#include "AS5600.h"
#include "MiniPID.h"     //https://github.com/tekdemo/MiniPID
#include "DCMotor.h"
#include "memory"
#include "SparkFun_I2C_Mux_Arduino_Library.h"

class MotorUnit {
  public:
    void begin(int forwardPin,
               int backwardPin,
               int readbackPin,
               int encoderAddress,
               int channel1,
               int channel2);
    void readEncoder();
    void zero();
    void setTarget(double newTarget);
    double getTarget();
    int setPosition(double newPosition);
    double getPosition();
    double getCurrent();
    double getError();
    void stop();
    void updateEncoderPosition();
    double recomputePID();
    void decompressBelt();
    bool comply(unsigned long *timeLastMoved, double *lastPosition, double *amtToMove, double maxSpeed);
    bool retract(double targetLength);
    double getCommandPWM();


  private:
    int _encoderAddress;
    AS5600 encoder;
    MiniPID positionPID; //These are the P,I,D values for the servo motors
    DCMotor motor;
    double setpoint = 0.0;
    double _mmPerRevolution = 43.975; //If the amount of belt extended is too long, this number needs to be bigger
    int _stallThreshold = 25; //The number of times in a row needed to trigger a warning
    int _stallCurrent = 27;   //The current threshold needed to count
    int _stallCount = 0;
    int _numPosErrors = 0; //Keeps track of the number of position errors in a row to detect a stall
    double _lastPosition = 0.0;
    double _commandPWM = 0; //The last PWM duty cycle sent to the motor
    double mostRecentCumulativeEncoderReading = 0;
    double encoderReadFailurePrintTime = millis();
    unsigned long lastCallGetPos = millis();
    QWIICMUX I2CMux;

};

#endif