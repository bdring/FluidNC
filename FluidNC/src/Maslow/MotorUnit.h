#ifndef MotorUnit_h
#define MotorUnit_h

#include "Arduino.h"
#include "Wire.h"
#include "AS5600.h"

class MotorUnit {
  public:
    void begin(int encoderAddress);
    void readEncoder();
  private:
    int _encoderAddress;
    AS5600 encoder;

};

#endif