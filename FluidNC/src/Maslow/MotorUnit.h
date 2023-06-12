#ifndef MotorUnit_h
#define MotorUnit_h

#include "Arduino.h"
#include "Wire.h"
#include "AS5600.h"
#include "MiniPID.h"     //https://github.com/tekdemo/MiniPID
#include "DCMotor.h"
#include "memory"

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
  private:
    int _encoderAddress;
    AS5600 encoder;
    std::unique_ptr<MiniPID> positionPID;
    DCMotor motor;

};

#endif