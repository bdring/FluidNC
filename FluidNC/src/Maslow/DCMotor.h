/*!
 *  @file DCMotor.h
 *
 *  This is a library to interact with the TI DRV8873 chip via a peripheral PWM generator chip (TLC59711)
 *
 */

#ifndef DCMotor_H
#define DCMotor_H

enum direction {BACKWARD, FORWARD};

#include <Arduino.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"

/*!
 *  @brief  Class that stores state and functions for interacting with
 *          DRV8873 Sensor
 */
class DCMotor{
public:
    DCMotor();      
    void begin(uint8_t forwardPin,
               uint8_t backwardPin,
               int readbackPin,
               int channel1,
               int channel2);
    void forward(uint16_t speed);
    void fullOut();
    void backward(uint16_t speed);
    void fullIn();
    void halfIn();
    void runAtSpeed(uint8_t direction, uint16_t speed);
    void runAtPWM(long signed_speed);
    void stop();
    void highZ();
    double readCurrent();

private:
    int multisamples = 1;
    uint8_t _forward, _back;
    int _readback;
    int _maxSpeed = 1023; //Absolute max is 1023
    int _channel1;
    int _channel2;
};

#endif
