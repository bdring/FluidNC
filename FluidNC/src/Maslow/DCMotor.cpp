// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

/***************************************************
 *   This is a library to interact with A DC Motor
 *
 *  By Bar Smith for Maslow CNC
 ****************************************************/

#include "DCMotor.h"

#define motorPWMFreq 2000
#define motorPWMRes 10

/*!
 *  @brief  Instantiates a new DCMotor class for generic two-wire control
 *  @param  forwardPin Output pin number for the motr. If this pin is at max
 *          output and the other pin is at 0 the motor turns forward
 *  @param  backwardPin Output pin number for the motor. If this pin is at
 *          max output and the other pin is at 0 the motor turns backward
 *  @param  readbackPin ESP32 adc_channel_t pin number for current read-back
 */
DCMotor::DCMotor() {}

void DCMotor::begin(uint8_t forwardPin, uint8_t backwardPin, int readbackPin, int channel1, int channel2) {
    _forward  = forwardPin;
    _back     = backwardPin;
    _readback = readbackPin;
    _channel1 = channel1;
    _channel2 = channel2;

    //Setup the motor controllers
    ledcSetup(channel1, motorPWMFreq, motorPWMRes);  // configure PWM functionalities...this uses timer 0 (channel, freq, resolution)
    ledcAttachPin(_forward, channel1);               // attach the channel to the GPIO to be controlled
    ledcWrite(channel1, 0);                          //Turn the motor off

    ledcSetup(channel2, motorPWMFreq, motorPWMRes);
    ledcAttachPin(_back, channel2);
    ledcWrite(channel2, 0);
}

/*!
 *  @brief  Run the motors forward at the given speed
 *  @param speed The speed the motor should spin (0-1023)
 */
void DCMotor::forward(uint16_t speed) {
    runAtSpeed(FORWARD, speed);
}

/*!
 *  @brief  Run the motors forward at max speed
 */
void DCMotor::fullOut() {
    runAtSpeed(FORWARD, _maxSpeed);
}

/*!
 *  @brief  Run the motors backward at the given speed
 *  @param speed The speed the motor should spin (0-1023)
 */
void DCMotor::backward(uint16_t speed) {
    runAtSpeed(BACKWARD, speed);
}

/*!
 *  @brief  Run the motors backward at max speed
 */
void DCMotor::fullIn() {
    runAtSpeed(BACKWARD, _maxSpeed);
}

/*!
 *  @brief  Run the motors backward at half max speed
 */
void DCMotor::halfIn() {
    runAtPWM(_maxSpeed / -2);
}

/*!
 *  @brief  Run the motors at the given speed. Interpret sign as backward for
 *  negative and forward for positive
 *  @param speed The speed the motor should spin (-1023 to 1023)
 */
void DCMotor::runAtPWM(long signed_speed) {
    //Motor driver accepts -maxPWMvalue to maxPWMvalue but doesn't begin moving until motorStartsToMovePWM so we scale

    int  motorStartsToMovePWM = 75;
    int  maxPWMvalue          = 1023;
    long scaledSpeed          = map(abs(signed_speed), 0, maxPWMvalue, motorStartsToMovePWM, _maxSpeed);

    if (signed_speed < 0) {
        runAtSpeed(BACKWARD, scaledSpeed);
    } else {
        runAtSpeed(FORWARD, scaledSpeed);
    }
}

/*!
 *  @brief  Run the motors in the given direction at the given speed. All other
 *  speed setting functions use this to actually write to the outputs
 *  @param  direction Direction backward (0) or forward (1, or ~0)
 *  @param speed The pwm frequency sent to the motor (0-1023)
 */
void DCMotor::runAtSpeed(uint8_t direction, uint16_t speed) {
    if (direction == 0) {
        ledcWrite(_channel1, _maxSpeed);
        ledcWrite(_channel2, _maxSpeed - speed);

    } else {
        ledcWrite(_channel2, _maxSpeed);
        ledcWrite(_channel1, _maxSpeed - speed);
    }
}

/*!
 *  @brief  Stop the motors in a braking state
 */
void DCMotor::stop() {
    //These could be set to 1023 to allow coasting
    ledcWrite(_channel1, 0);  //Stop
    ledcWrite(_channel2, 0);
}

/*!
 *  @brief  Stop the motors in a high-z state
 */
void DCMotor::highZ() {
    ledcWrite(_channel1, 0);  //Stop
    ledcWrite(_channel2, 0);
}

/*!
 *  @brief  Read the value from an ADC and calculate the current. Allows
 *  multisampling to smooth signal
 *  NOTE: ESP32 adcs are non-linear and have deadzones at top and bottom.
 *        This value bottoms out above 0mA!
 *  @return Calibrated reading of current in mA.
 *  NOTE: Converted to return percentage. Actual accuracy is not particularly important.
 *
 */
double DCMotor::readCurrent() {
    return analogRead(_readback);
}
