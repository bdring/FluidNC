// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "MotorUnit.h"
#include "../Report.h"
#include "Maslow.h"

// PID controller tuning
#define P 300
#define I 0
#define D 0

//------------------------------------------------------
//------------------------------------------------------ Core utility functions
//------------------------------------------------------

void MotorUnit::begin(int forwardPin, int backwardPin, int readbackPin, int encoderAddress, int channel1, int channel2) {
    _encoderAddress = encoderAddress;

    Maslow.I2CMux.setPort(_encoderAddress);
    if (!encoder.begin()) {
        log_error("Encoder not found on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
        Maslow.error = true;
        Maslow.errorMessage = "Encoder not found on " + Maslow.axis_id_to_label(_encoderAddress);
    } else {
        log_info("Encoder connected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    }
    zero();

    motor.begin(forwardPin, backwardPin, readbackPin, channel1, channel2);

    positionPID.setPID(P, I, D);
    positionPID.setOutputLimits(-1023, 1023);

    if (!motor_test()) {
        log_error("Motor not found on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
        Maslow.error = true;
        Maslow.errorMessage = "Motor not found on " + Maslow.axis_id_to_label(_encoderAddress);
    } else {
        log_info("Motor detected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    }
}

//Test the motor unit by testing the motor and checking the encoder
bool MotorUnit::test() {
    //Check if the motor / motor driver are connected
    if (!motor_test()) {
        log_warn("Motor not found on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
        Maslow.error = true;
        Maslow.errorMessage = "Motor not found on " + Maslow.axis_id_to_label(_encoderAddress);
    } else {
        log_info("Motor detected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    }

    //Check if the encoder is connected
    if (!updateEncoderPosition()) {
        log_warn("Encoder not found on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
        Maslow.error = true;
        Maslow.errorMessage = "Encoder not found on " + Maslow.axis_id_to_label(_encoderAddress);
    } else {
        log_info("Encoder connected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    }

    //Check for the presence of the magnet
    if (!encoder.detectMagnet()) {
        log_warn("Magnet not detected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    } else {
        log_info("Magnet detected on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
    }

    return !Maslow.error;
}

bool MotorUnit::motor_test() {
    //run motor for 100ms and check if the current gets above zero
    unsigned long time        = millis();
    unsigned long elapsedTime = millis() - time;
    while (elapsedTime < 100) {
        elapsedTime = millis() - time;
        motor.forward(1023);
        if (motor.readCurrent() > 30) {
            motor.stop();
            return true;
        }
    }
    motor.stop();
    return false;
}
// update the motor current buffer and belts speed every >5 ms
void MotorUnit::update() {
    //updating belt speed and motor cutrrent
    //update belt speed every 50ms or so:
    if (millis() - beltSpeedTimer > 50) {
        beltSpeed             = (getPosition() - beltSpeedLastPosition) / ((millis() - beltSpeedTimer) / 1000.0);  // mm/s
        beltSpeedTimer        = millis();
        beltSpeedLastPosition = getPosition();
    }

    if (millis() - motorCurrentTimer > 5) {
        motorCurrentTimer = millis();
        for (int i = 0; i < 9; i++) {
            motorCurrentBuffer[i] = motorCurrentBuffer[i + 1];
        }
        motorCurrentBuffer[9] = motor.readCurrent();
    }
}

// Reads the encoder value and updates it's position
bool MotorUnit::updateEncoderPosition() {
    if (!Maslow.I2CMux.setPort(_encoderAddress))
        return false;

    if (encoder.isConnected()) {                                               //this func has 50ms timeout (or worse?, hard to tell)
        mostRecentCumulativeEncoderReading = encoder.getCumulativePosition();  //This updates and returns the encoder value
        return true;
    } else if (millis() - encoderReadFailurePrintTime > 5000) {
        encoderReadFailurePrintTime = millis();
        log_warn("Encoder read failure on " << Maslow.axis_id_to_label(_encoderAddress).c_str());
        //Maslow.panic();
    }
    return false;
}

/*!
 *  @brief  Gets the current error in the axis position
 */
double MotorUnit::getPositionError() {
    return getPosition() - setpoint;
}

// Recomputes the PID and drives the output
double MotorUnit::recomputePID() {
    _commandPWM = positionPID.getOutput(getPosition(), setpoint);

    motor.runAtPWM(_commandPWM);

    return _commandPWM;
}

//------------------------------------------------------
//------------------------------------------------------ Homing/calibration functions
//------------------------------------------------------

// Sets the motor to comply with how it is being pulled, non-blocking.
bool MotorUnit::comply() {
    //Call it every 25 ms
    if (millis() - lastCallToComply < 25) {
        return true;
    }

    //If we've moved any, then drive the motor outwards to extend the belt
    float positionNow = getPosition();
    float distMoved   = positionNow - lastPosition;

    //If the belt is moving out, let's keep it moving out
    if (distMoved > .1) {  //EXPERIMENTAL

        motor.forward(amtToMove);

        if (amtToMove < 100)
            amtToMove = 100;
        amtToMove = amtToMove * 1.4;

        amtToMove = min(amtToMove, 1023.0);
    }

    //Finally if the belt is not moving we want to spool things down
    else {
        amtToMove = amtToMove / 1.25;
        motor.forward(amtToMove);
    }

    _commandPWM  = amtToMove;  //update actual motor power, so the get motor power isn't lying to us
    lastPosition = positionNow;

    lastCallToComply = millis();
    return true;
}

// Pulls_tight and zeros axis; returns true when done
bool MotorUnit::retract() {
    if (pull_tight(Maslow.retractCurrentThreshold)) {
        log_info(Maslow.axis_id_to_label(_encoderAddress).c_str() << " pulled tight with offset " << getPosition());
        zero();
        return true;
    }
    return false;
}

// Pulls the belt until we hit a current treshold; returns true when done
bool MotorUnit::pull_tight(int currentThreshold) {
    //call at most every 5ms
    if (millis() - lastCallToRetract < 4) {
        return false;
    }
    lastCallToRetract = millis();

    //Gradually increase the pulling speed
    if (random(0, 2) == 1){
        retract_speed = min(retract_speed + 1, 1023);
    }

    motor.backward(retract_speed);
    _commandPWM = -retract_speed; //This is only used for the getPWM function. There's got to be a tidier way to do this.

    //When taught
    int currentMeasurement = motor.readCurrent();

    retract_baseline = alpha * float(currentMeasurement) + (1 - alpha) * retract_baseline;

    if (currentMeasurement - retract_baseline > incrementalThreshold) {
        incrementalThresholdHits = incrementalThresholdHits + 1;
    } else {
        incrementalThresholdHits = 0;
    }


    if (retract_speed > 15) { //20 is not the actual speed, it is the amount of time so we don't trigger immediately
        if (currentMeasurement > currentThreshold || incrementalThresholdHits > 2) {
            //stop motor, reset variables
            stop();
            retract_speed    = 0;
            retract_baseline = 700;
            return true;
        } else {
            return false;
        }
    }
    return false;
}

// extends the belt to the target length until it hits the target length, returns true when target length is reached
bool MotorUnit::extend(double targetLength) {
    //unsigned long timeLastMoved = millis();

    if (getPosition() < targetLength) {
        comply();  //Comply does the actual moving
        return false;
    }
    //If reached target position, Stop and return
    setTarget(getPosition());
    stop();

    //log_info("Belt positon after extend: ");
    //log_info(getPosition());
    return true;
}

//------------------------------------------------------
//------------------------------------------------------ Utility functions
//------------------------------------------------------

// Sets the target location in mm
void MotorUnit::setTarget(double newTarget) {
    setpoint = newTarget;
}

// Gets the target location in mm
double MotorUnit::getTarget() {
    return setpoint;
}

// Returns the current position of the axis in mm
double MotorUnit::getPosition() {
    double positionNow = (mostRecentCumulativeEncoderReading / 4096.0) * _mmPerRevolution * -1;
    return positionNow;
}

// Returns the current motor power draw
double MotorUnit::getCurrent() {
    return motor.readCurrent();
}

// Stops the motor
void MotorUnit::stop() {
    motor.stop();
    _commandPWM = 0;
}

// Returns the PWM values set to the motor
double MotorUnit::getMotorPower() {
    return _commandPWM;
}

// Returns current belts speed, remove? (TODO)
double MotorUnit::getBeltSpeed() {
    return beltSpeed;
}

// Returns average motor current over last 10 reads
double MotorUnit::getMotorCurrent() {
    //return average motor current of the last 10 readings:
    double sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += motorCurrentBuffer[i];
    }
    return sum / 10.0;
}

// Checking if we are at the target position within certain precision:
bool MotorUnit::onTarget(double precision) {
    if (abs(getTarget() - getPosition()) < precision)
        return true;
    else
        return false;
}

//Runs the motor to extend at full speed
void MotorUnit::decompressBelt() {
    int decompressSpeed = 800;
    motor.forward(decompressSpeed);
    _commandPWM = decompressSpeed;
}

//Runs the motor at full speed out
void MotorUnit::fullOut() {
    motor.fullOut();
    _commandPWM = 1023;
}
//Runs the motor at full speed in
void MotorUnit::fullIn() {
    motor.fullIn();
    _commandPWM = 1023;
}

// Reset all the axis variables
void MotorUnit::reset() {
    retract_speed            = 0;
    retract_baseline         = 700;
    incrementalThresholdHits = 0;
    amtToMove                = 0;
    lastPosition             = getPosition();
    beltSpeedTimer           = millis();
}

//sets the encoder position to 0
void MotorUnit::zero() {
    Maslow.I2CMux.setPort(_encoderAddress);
    encoder.resetCumulativePosition();
}
