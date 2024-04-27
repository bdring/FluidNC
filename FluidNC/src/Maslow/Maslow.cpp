// Copyright (c) 2014 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "Maslow.h"
#include "../Report.h"
#include "../WebUI/WifiConfig.h"

// Maslow specific defines
#define VERSION_NUMBER "0.70"

#define TLEncoderLine 2
#define TREncoderLine 1
#define BLEncoderLine 3
#define BREncoderLine 0

#define motorPWMFreq 2000
#define motorPWMRes 10

#define tlIn1Pin 45
#define tlIn1Channel 0
#define tlIn2Pin 21
#define tlIn2Channel 1
#define tlADCPin 18

#define trIn1Pin 42
#define trIn1Channel 2
#define trIn2Pin 41
#define trIn2Channel 3
#define trADCPin 6

#define blIn1Pin 37
#define blIn1Channel 4
#define blIn2Pin 36
#define blIn2Channel 5
#define blADCPin 8

#define brIn1Pin 9
#define brIn1Channel 6
#define brIn2Pin 3
#define brIn2Channel 7
#define brADCPin 7

#define coolingFanPin 47

#define SERVOFAULT 40

#define ETHERNETLEDPIN 39
#define WIFILED 35
#define REDLED 14

int ENCODER_READ_FREQUENCY_HZ = 1000;  //max frequency for polling the encoders

//------------------------------------------------------
//------------------------------------------------------ Main function loops
//------------------------------------------------------

// Initialization function
void Maslow_::begin(void (*sys_rt)()) {
    Wire.begin(5, 4, 400000);
    I2CMux.begin(TCAADDR, Wire);

    axisTL.begin(tlIn1Pin, tlIn2Pin, tlADCPin, TLEncoderLine, tlIn1Channel, tlIn2Channel);
    axisTR.begin(trIn1Pin, trIn2Pin, trADCPin, TREncoderLine, trIn1Channel, trIn2Channel);
    axisBL.begin(blIn1Pin, blIn2Pin, blADCPin, BLEncoderLine, blIn1Channel, blIn2Channel);
    axisBR.begin(brIn1Pin, brIn2Pin, brADCPin, BREncoderLine, brIn1Channel, brIn2Channel);

    axisBLHomed = false;
    axisBRHomed = false;
    axisTRHomed = false;
    axisTLHomed = false;

    //Recompute the center XY
    updateCenterXY();

    pinMode(coolingFanPin, OUTPUT);
    pinMode(ETHERNETLEDPIN, OUTPUT);
    pinMode(WIFILED, OUTPUT);
    pinMode(REDLED, OUTPUT);

    digitalWrite(ETHERNETLEDPIN, LOW);

    pinMode(SERVOFAULT, INPUT);

    currentThreshold = 1500;
    lastCallToUpdate = millis();
    
    if (error) {
        log_error("Maslow failed to initialize - fix errors and restart");
    } else
        log_info("Starting Maslow Version " << VERSION_NUMBER);
}

// Maslow main loop, everything is processed here
void Maslow_::update() {
    if (error) {
        static unsigned long timer = millis();
        static bool          st    = true; //This is used to blink the LED
        if (millis() - timer > 300) {
            stopMotors();
            st = !st;
            digitalWrite(REDLED, st);
            timer = millis();
            log_error(errorMessage.c_str());
        }
        return;
    }
    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, LOW);
    }

    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, HIGH);
    }

    blinkIPAddress();

    heartBeat();

    //Make sure we're running maslow config file
    if (!Maslow.using_default_config) {
        lastCallToUpdate = millis();

        Maslow.updateEncoderPositions();  //We always update encoder positions in any state,

        axisTL.update();  //update motor currents and belt speeds like this for now
        axisTR.update();
        axisBL.update();
        axisBR.update();

        if (safetyOn)
            safety_control();

        //quick solution for delay without blocking
        if (holding && millis() - holdTimer > holdTime) {
            holding = false;
        } else if (holding)
            return;

        //temp test function
        if (test) {
            test = false;
        }

        //------------------------ Maslow State Machine

        //-------Jog or G-code execution. Maybe need to add more modes here like Hold?
        //Jog doesn't work for lack of feedback, HOLD doesn't get automatically called after jog, so IDK what (TODO)
        if (sys.state() == State::Jog || sys.state() == State::Cycle) {
            Maslow.setTargets(steps_to_mpos(get_axis_motor_steps(0), 0),
                              steps_to_mpos(get_axis_motor_steps(1), 1),
                              steps_to_mpos(get_axis_motor_steps(2), 2));

            //This allows the z-axis to be moved without the motors being enabled before calibration is run
            if (allAxisExtended()) {
                Maslow.recomputePID();
            }
        }

        //--------Homing routines
        else if (sys.state() == State::Homing) {
            home();
        } else {  //In any other state, keep motors off
            if (!test)
                Maslow.stopMotors();
        }

        //If we are in any state other than idle or alarm turn the cooling fan on
        if (sys.state() != State::Idle && sys.state() != State::Alarm) {
            digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on
        }
        //If we are doing calibration turn the cooling fan on
        else if (calibrationInProgress || extendingALL || retractingTL || retractingTR || retractingBL || retractingBR) {
            digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on
        } else {
            digitalWrite(coolingFanPin, LOW);  //Turn the cooling fan off
        }

        //Check to see if we need to resend the calibration data
        checkCalibrationData();

        //------------------------ End of Maslow State Machine

        //if the update function is not being called enough, stop everything to prevent damage
        if (millis() - lastCallToUpdate > 100) {
            Maslow.panic();
            int elapsedTime = millis() - lastCallToUpdate;
            log_error("Emergency stop. Update function not being called enough." << elapsedTime << "ms since last call");
        }
    }
}

void Maslow_::blinkIPAddress() {
    static size_t        currentChar   = 0;
    static int           currentBlink  = 0;
    static unsigned long lastBlinkTime = 0;
    static bool          inPause       = false;

    int shortMS = 400;
    int longMS  = 500;
    int pauseMS = 2000;

    std::string IP_String = WebUI::wifi_config.getIP();

    if (currentChar >= IP_String.length()) {
        currentChar   = 0;
        currentBlink  = 0;
        lastBlinkTime = 0;
        inPause       = false;
        digitalWrite(WIFILED, LOW);
        return;
    }

    char c = IP_String[currentChar];
    if (isdigit(c)) {
        int blinkCount = c - '0';
        if (currentBlink < blinkCount * 2) {
            if (millis() - lastBlinkTime >= shortMS) {
                //log_info("Blinking Digit: " << c);
                digitalWrite(WIFILED, currentBlink % 2 == 0 ? HIGH : LOW);
                currentBlink++;
                lastBlinkTime = millis();
            }
        } else if (!inPause) {
            inPause       = true;
            lastBlinkTime = millis();
        } else if (millis() - lastBlinkTime >= pauseMS) {
            inPause = false;
            currentChar++;
            currentBlink = 0;
        }
    } else if (c == '.') {
        if (millis() - lastBlinkTime >= longMS) {
            digitalWrite(WIFILED, LOW);
            currentChar++;
            currentBlink = 0;
        } else {
            digitalWrite(WIFILED, HIGH);
        }
    }
}

void Maslow_::heartBeat(){
    static unsigned long heartBeatTimer = millis();

    if(millis() - heartBeatTimer > 1000 && HeartBeatEnabled) {
        heartBeatTimer = millis();
        log_info("Heartbeat");
    }
}

// -Maslow homing loop
void Maslow_::home() {
    //run all the retract functions untill we hit the current limit
    if (retractingTL) {
        if (axisTL.retract()) {
            retractingTL  = false;
            axis_homed[0] = true;
            extendedTL    = false;
        }
    }
    if (retractingTR) {
        if (axisTR.retract()) {
            retractingTR  = false;
            axis_homed[1] = true;
            extendedTR    = false;
        }
    }
    if (retractingBL) {
        if (axisBL.retract()) {
            retractingBL  = false;
            axis_homed[2] = true;
            extendedBL    = false;
        }
    }
    if (retractingBR) {
        if (axisBR.retract()) {
            retractingBR  = false;
            axis_homed[3] = true;
            extendedBR    = false;
        }
    }

    // $EXT - extend mode
    if (extendingALL) {
        //decompress belts for the first half second
        if (millis() - extendCallTimer < 700) {
            if (millis() - extendCallTimer > 0)
                axisBR.decompressBelt();
            if (millis() - extendCallTimer > 150)
                axisBL.decompressBelt();
            if (millis() - extendCallTimer > 250)
                axisTR.decompressBelt();
            if (millis() - extendCallTimer > 350)
                axisTL.decompressBelt();
        }
        //then make all the belts comply until they are extended fully, or user terminates it
        else {
            if (!extendedTL)
                extendedTL = axisTL.extend(computeTL(0, 0, 0));
            if (!extendedTR)
                extendedTR = axisTR.extend(computeTR(0, 0, 0));
            if (!extendedBL)
                extendedBL = axisBL.extend(computeBL(0, 300, 0));
            if (!extendedBR)
                extendedBR = axisBR.extend(computeBR(0, 300, 0));
            if (extendedTL && extendedTR && extendedBL && extendedBR) {
                extendingALL = false;
                log_info("All belts extended to center position");
            }
        }
    }
    // $CMP - comply mode
    if (complyALL) {
        //decompress belts for the first half second
        if (millis() - complyCallTimer < 40) {
            axisBR.decompressBelt();
            axisBL.decompressBelt();
            axisTR.decompressBelt();
            axisTL.decompressBelt();
        } else if(millis() - complyCallTimer < 800){
            axisTL.comply();
            axisTR.comply();
            axisBL.comply();
            axisBR.comply();
        }
        else {
            complyALL = false;
        }
    }

    // $CAL - calibration mode
    if (calibrationInProgress) {
        calibration_loop();
    }
    // Runs the take slack sequence
    if(takeSlack){
        if (takeSlackFunc()) {
            takeSlack = false;
        }
    }

    handleMotorOverides();

    //if we are done with all the homing moves, switch system state back to Idle?
    if (!retractingTL && !retractingBL && !retractingBR && !retractingTR && !extendingALL && !complyALL && !calibrationInProgress &&
        !takeSlack && !checkOverides()) {
        sys.set_state(State::Idle);
    }
}

//Moves to 0,0 takes a measurement
bool Maslow_::takeSlackFunc() {
    static int takeSlackState = 0; //0 -> Starting, 1-> Moving to (0,0), 2-> Taking a measurement
    static unsigned long holdTimer = millis();
    static float startingX    = 0;
    static float startingY    = 0;

    //Initialize
    if (takeSlackState == 0) {
        takeSlackState = 1;
        startingX   = getTargetX();
        startingY   = getTargetY();
    }

    //Move to (0,0)
    if(takeSlackState == 1){
        if (move_with_slack(startingX, startingY, 0, 0)) {
            takeSlackState = 2;
        }
    }

    //Take a measurement
    if(takeSlackState == 2){
        if (take_measurement_avg_with_check(0, UP)) {

            double offset = _beltEndExtension + _armLength;
            double threshold = 15;

            float diffTL = calibration_data[0][0] - offset - computeTL(0, 0, 0);
            float diffTR = calibration_data[1][0] - offset - computeTR(0, 0, 0);
            float diffBL = calibration_data[2][0] - offset - computeBL(0, 0, 0);
            float diffBR = calibration_data[3][0] - offset - computeBR(0, 0, 0);
            log_info("Center point deviation: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);
            if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                log_error("Center point deviation over " << threshold << "mmm, your coordinate system is not accurate, maybe try running calibration again?");
                //Should we enter an alarm state here to prevent things from going wrong?
                
                //Reset
                takeSlackState = 0;
                return true; 
            }
            else{
                log_info("Center point deviation within " << threshold << "mm, your coordinate system is accurate");
                takeSlackState = 3;
                holdTimer = millis();
            }
        }
    }

    //Position hold for 2 seconds
    if(takeSlackState == 3){
        if(millis() - holdTimer > 2000){
            takeSlackState = 0;
            return true;
        }
    }



    return false;
}

// --Maslow calibration loop
void Maslow_::calibration_loop() {
    static int  direction             = UP;
    static bool measurementInProgress = false;
    if(waypoint > pointCount){
        calibrationInProgress = false;
        waypoint              = 0;
        log_info("Calibration complete");
        return;
    }
    //Taking measurment once we've reached the point
    if (measurementInProgress) {
        if (take_measurement_avg_with_check(waypoint, direction)) {  //Takes a measurement and returns true if it's done
            measurementInProgress = false;
            
            waypoint++;  //Increment the waypoint counter

            if (waypoint > recomputePoints[recomputeCountIndex]) {  //If we have reached the end of this stage of the calibration process
                calibrationInProgress = false;
                print_calibration_data();
                calibrationDataWaiting = millis();
                sys.set_state(State::Idle);
                recomputeCountIndex++;
            }
            else {
                hold(250);
            }
        }
    }

    //Move to the next point in the grid
    else {
        if (move_with_slack(calibrationGrid[waypoint - 1][0],
                            calibrationGrid[waypoint - 1][1],
                            calibrationGrid[waypoint][0],
                            calibrationGrid[waypoint][1])) {
            measurementInProgress = true;
            direction             = get_direction(calibrationGrid[waypoint - 1][0],
                                      calibrationGrid[waypoint - 1][1],
                                      calibrationGrid[waypoint][0],
                                      calibrationGrid[waypoint][1]);
            x                     = calibrationGrid[waypoint][0];
            y                     = calibrationGrid[waypoint][1];
            hold(250);
        }
    }
}

//------------------------------------------------------
//------------------------------------------------------ Core utility functions
//------------------------------------------------------

//updating encoder positions for all 4 arms, cycling through them each call, at ENCODER_READ_FREQUENCY_HZ frequency
bool Maslow_::updateEncoderPositions() {
    bool                 success               = true;
    static unsigned long lastCallToEncoderRead = millis();

    static int           encoderFailCounter[4] = { 0, 0, 0, 0 };
    static unsigned long encoderFailTimer      = millis();

    if (!readingFromSD && (millis() - lastCallToEncoderRead > 1000 / (ENCODER_READ_FREQUENCY_HZ))) {
        static int encoderToRead = 0;
        switch (encoderToRead) {
            case 0:
                if (!axisTL.updateEncoderPosition()) {
                    encoderFailCounter[TLEncoderLine]++;
                }
                break;
            case 1:
                if (!axisTR.updateEncoderPosition()) {
                    encoderFailCounter[TREncoderLine]++;
                }
                break;
            case 2:
                if (!axisBL.updateEncoderPosition()) {
                    encoderFailCounter[BLEncoderLine]++;
                }
                break;
            case 3:
                if (!axisBR.updateEncoderPosition()) {
                    encoderFailCounter[BREncoderLine]++;
                }
                break;
        }
        encoderToRead++;
        if (encoderToRead > 3) {
            encoderToRead         = 0;
            lastCallToEncoderRead = millis();
        }
    }

    // if more than 1% of readings fail, warn user, if more than 10% fail, stop the machine and raise alarm
    if (millis() - encoderFailTimer > 1000) {
        for (int i = 0; i < 4; i++) {
            //turn i into proper label
            String label = axis_id_to_label(i);
            if (encoderFailCounter[i] > 0.1 * ENCODER_READ_FREQUENCY_HZ) {
                // log error statement with appropriate label
                log_error("Failure on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i]
                                        << " times in the last second");
                Maslow.panic();
            } else if (encoderFailCounter[i] > 0) {  //0.01*ENCODER_READ_FREQUENCY_HZ){
                log_warn("Bad connection on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i]
                                              << " times in the last second");
            }
            encoderFailCounter[i] = 0;
            encoderFailTimer      = millis();
        }
    }

    return success;
}

// This computes the target lengths of the belts based on the target x and y coordinates and sends that information to each arm.
void Maslow_::setTargets(float xTarget, float yTarget, float zTarget, bool tl, bool tr, bool bl, bool br) {
    //Store the target x and y coordinates for the getTargetN() functions
    targetX = xTarget;
    targetY = yTarget;
    targetZ = zTarget;

    if (tl) {
        axisTL.setTarget(computeTL(xTarget, yTarget, zTarget));
    }
    if (tr) {
        axisTR.setTarget(computeTR(xTarget, yTarget, zTarget));
    }
    if (bl) {
        axisBL.setTarget(computeBL(xTarget, yTarget, zTarget));
    }
    if (br) {
        axisBR.setTarget(computeBR(xTarget, yTarget, zTarget));
    }
}

//updates motor powers for all axis, based on targets set by setTargets()
void Maslow_::recomputePID() {
    axisBL.recomputePID();
    axisBR.recomputePID();
    axisTR.recomputePID();
    axisTL.recomputePID();

    digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on

    if (digitalRead(SERVOFAULT) ==
        1) {  //The servo drives have a fault pin that goes high when there is a fault (ie one over heats). We should probably call panic here. Also this should probably be read in the main loop
        log_info("Servo fault!");
    }
}

//This is the function that should prevent machine from damaging itself
void Maslow_::safety_control() {
    //We need to keep track of average belt speeds and motor currents for every axis
    static bool          tick[4]                 = { false, false, false, false };
    static unsigned long spamTimer               = millis();
    static int           tresholdHitsBeforePanic = 15;
    static int           panicCounter[4]         = { 0 };

    MotorUnit* axis[4] = { &axisTL, &axisTR, &axisBL, &axisBR };
    for (int i = 0; i < 4; i++) {
        //If the current exceeds some absolute value, we need to call panic() and stop the machine
        if (axis[i]->getMotorCurrent() > 4000 && !tick[i]) {
            panicCounter[i]++;
            if (panicCounter[i] > tresholdHitsBeforePanic) {
                log_error("Motor current on " << axis_id_to_label(i).c_str() << " axis exceeded threshold of " << 4000);
                if(!calibrationInProgress){
                    Maslow.panic();
                }
                tick[i] = true;
            }
        } else {
            panicCounter[i] = 0;
        }

        //If the motor torque is high, but the belt is not moving
        //  if motor is moving IN, this means the axis is STALL, we should warn the user and lower torque to the motor
        //  if the motor is moving OUT, that means the axis has SLACK, so we should warn the user and stop the motor, until the belt starts moving again
        // don't spam log, no more than once every 5 seconds

        static int axisSlackCounter[4] = { 0, 0, 0, 0 };

        axisSlackCounter[i] = 0;  //TEMP
        if (axis[i]->getMotorPower() > 450 && abs(axis[i]->getBeltSpeed()) < 0.1 && !tick[i]) {
            axisSlackCounter[i]++;
            if (axisSlackCounter[i] > 3000) {
                // log_info("SLACK:" << axis_id_to_label(i).c_str() << " motor power is " << int(axis[i]->getMotorPower())
                //                   << ", but the belt speed is" << axis[i]->getBeltSpeed());
                // log_info(axisSlackCounter[i]);
                // log_info("Pull on " << axis_id_to_label(i).c_str() << " and restart!");
                tick[i]             = true;
                axisSlackCounter[i] = 0;
                Maslow.panic();
            }
        } else
            axisSlackCounter[i] = 0;

        //If the motor has a position error greater than 1mm and we are running a file or jogging
        if ((abs(axis[i]->getPositionError()) > 1) && (sys.state() == State::Jog || sys.state() == State::Cycle) && !tick[i]) {
            // log_error("Position error on " << axis_id_to_label(i).c_str() << " axis exceeded 1mm, error is " << axis[i]->getPositionError()
            //                                << "mm");
            tick[i] = true;
        }
        if ((abs(axis[i]->getPositionError()) > 15) && (sys.state() == State::Cycle)) {
            log_error("Position error on " << axis_id_to_label(i).c_str() << " axis exceeded 15mm while running. Halting. Error is "
                                           << axis[i]->getPositionError() << "mm");
            Maslow.eStop("Position error > 15mm while running. E-Stop triggered.");
        }
    }

    if (millis() - spamTimer > 5000) {
        for (int i = 0; i < 4; i++) {
            tick[i] = false;
        }
        spamTimer = millis();
    }
}

// Compute target belt lengths based on X-Y-Z coordinates
float Maslow_::computeBL(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = blX - x;
    float b = blY - y;
    float c = 0.0 - (z + blZ);

    float length = sqrt(a * a + b * b + c * c) - (_beltEndExtension + _armLength);

    return length;  //+ lowerBeltsExtra;
}
float Maslow_::computeBR(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = brX - x;
    float b = brY - y;
    float c = 0.0 - (z + brZ);

    float length = sqrt(a * a + b * b + c * c) - (_beltEndExtension + _armLength);

    return length;  //+ lowerBeltsExtra;
}
float Maslow_::computeTR(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = trX - x;
    float b = trY - y;
    float c = 0.0 - (z + trZ);
    return sqrt(a * a + b * b + c * c) - (_beltEndExtension + _armLength);
}
float Maslow_::computeTL(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = tlX - x;
    float b = tlY - y;
    float c = 0.0 - (z + tlZ);
    return sqrt(a * a + b * b + c * c) - (_beltEndExtension + _armLength);
}

//------------------------------------------------------
//------------------------------------------------------ Homing and calibration functions
//------------------------------------------------------

// Takes one measurement; returns true when it's done. Waypoint # is used to st
bool Maslow_::take_measurement(int waypoint, int dir, int run) {

    //Shouldn't this be handled with the same code as below but with the direction set to UP?
    if (orientation == VERTICAL) {
        //first we pull two bottom belts tight one after another, if x<0 we pull left belt first, if x>0 we pull right belt first
        static bool BL_tight = false;
        static bool BR_tight = false;
        axisTL.recomputePID();
        axisTR.recomputePID();

        //On the left side of the sheet we want to pull the left belt tight first
        if (x < 0) {
            if (!BL_tight) {
                if (axisBL.pull_tight(calibrationCurrentThreshold)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
            if (!BR_tight) {
                if (axisBR.pull_tight(calibrationCurrentThreshold)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
        }

        //On the right side of the sheet we want to pull the right belt tight first
        else {
            if (!BR_tight) {
                if (axisBR.pull_tight(calibrationCurrentThreshold)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
            if (!BL_tight) {
                if (axisBL.pull_tight(calibrationCurrentThreshold)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
        }

        //once both belts are pulled, take a measurement
        if (BR_tight && BL_tight) {
            //take measurement and record it to the calibration data array
            calibration_data[0][waypoint] = axisTL.getPosition() + _beltEndExtension + _armLength;
            calibration_data[1][waypoint] = axisTR.getPosition() + _beltEndExtension + _armLength;
            calibration_data[2][waypoint] = axisBL.getPosition() + _beltEndExtension + _armLength;
            calibration_data[3][waypoint] = axisBR.getPosition() + _beltEndExtension + _armLength;
            BR_tight                      = false;
            BL_tight                      = false;
            return true;
        }
        return false;
    }
    // in HoRIZONTAL orientation we pull on the belts depending on the direction of the last move. This is important because the other two belts are likely slack
    else if (orientation == HORIZONTAL) {
        static MotorUnit* pullAxis1;
        static MotorUnit* pullAxis2;
        static MotorUnit* holdAxis1;
        static MotorUnit* holdAxis2;
        static bool       pull1_tight = false;
        static bool       pull2_tight = false;
        switch (dir) {
            case UP:
                holdAxis1 = &axisTL;
                holdAxis2 = &axisTR;
                if (x < 0) {
                    pullAxis1 = &axisBL;
                    pullAxis2 = &axisBR;
                } else {
                    pullAxis1 = &axisBR;
                    pullAxis2 = &axisBL;
                }
                break;
            case DOWN:
                holdAxis1 = &axisBL;
                holdAxis2 = &axisBR;
                if (x < 0) {
                    pullAxis1 = &axisTL;
                    pullAxis2 = &axisTR;
                } else {
                    pullAxis1 = &axisTR;
                    pullAxis2 = &axisTL;
                }
                break;
            case LEFT:
                holdAxis1 = &axisTL;
                holdAxis2 = &axisBL;
                if (y < 0) {
                    pullAxis1 = &axisBR;
                    pullAxis2 = &axisTR;
                } else {
                    pullAxis1 = &axisTR;
                    pullAxis2 = &axisBR;
                }
                break;
            case RIGHT:
                holdAxis1 = &axisTR;
                holdAxis2 = &axisBR;
                if (y < 0) {
                    pullAxis1 = &axisBL;
                    pullAxis2 = &axisTL;
                } else {
                    pullAxis1 = &axisTL;
                    pullAxis2 = &axisBL;
                }
                break;
        }
        holdAxis1->recomputePID();
        holdAxis2->recomputePID();

        if(run == 0){
            if (!pull1_tight) {
                if (pullAxis1->pull_tight(calibrationCurrentThreshold)) {
                    pull1_tight      = true;
                }
                if (run == 0) //Second axis complies while first is pulling
                    pullAxis2->comply();
                return false;
            }
            if (!pull2_tight) {
                if (pullAxis2->pull_tight(calibrationCurrentThreshold)) {
                    pull2_tight      = true;
                }
                return false;
            }
        }
        else{
            if (pullAxis1->pull_tight(calibrationCurrentThreshold)) {
                pull1_tight      = true;
            }
            if (pullAxis2->pull_tight(calibrationCurrentThreshold)) {
                pull2_tight      = true;
            }
        }
        if (pull1_tight && pull2_tight) {
            //take measurement and record it to the calibration data array
            calibration_data[0][waypoint] = axisTL.getPosition() + _beltEndExtension + _armLength;
            calibration_data[1][waypoint] = axisTR.getPosition() + _beltEndExtension + _armLength;
            calibration_data[2][waypoint] = axisBL.getPosition() + _beltEndExtension + _armLength;
            calibration_data[3][waypoint] = axisBR.getPosition() + _beltEndExtension + _armLength;
            pull1_tight                   = false;
            pull2_tight                   = false;
            return true;
        }
    }

    return false;
}

// Takes a series of measurements, calculates average and records calibration data;  returns true when it's done
bool Maslow_::take_measurement_avg_with_check(int waypoint, int dir) {
    //take 5 measurements in a row, (ignoring the first one), if they are all within 1mm of each other, take the average and record it to the calibration data array
    static int           run                = 0;
    static double        measurements[4][4] = { 0 };
    static double        avg                = 0;
    static double        sum                = 0;

    if (take_measurement(waypoint, dir, run)) {
        if (run < 2) {
            run++;
            return false;  //discard the first three measurements
        }

        measurements[0][run - 2] = calibration_data[0][waypoint];  //-3 cuz discarding the first 3 measurements
        measurements[1][run - 2] = calibration_data[1][waypoint];
        measurements[2][run - 2] = calibration_data[2][waypoint];
        measurements[3][run - 2] = calibration_data[3][waypoint];

        run++;

        static int criticalCounter = 0;
        if (run > 5) {
            run = 0;

            //check if all measurements are within 1mm of each other
            double maxDeviation[4] = { 0 };
            double maxDeviationAbs = 0;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 3; j++) {
                    //find max deviation between measurements
                    maxDeviation[i] = max(maxDeviation[i], abs(measurements[i][j] - measurements[i][j + 1]));
                }
            }

            for (int i = 0; i < 4; i++) {
                maxDeviationAbs = max(maxDeviationAbs, maxDeviation[i]);
            }
            if (maxDeviationAbs > 2.5) {
                log_error("Measurement error, measurements are not within 2.5 mm of each other, trying again");
                log_info("Max deviation: " << maxDeviationAbs);

                //print all the measurements in readable form:
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        //use axis id to label:
                        log_info(axis_id_to_label(i).c_str() << " " << measurements[i][j]);
                    }
                }
                //reset the run counter to run the measurements again
                if (criticalCounter++ > 8) { //This updates the counter and checks
                    log_error("Critical error, measurements are not within 1.5mm of each other 8 times in a row, stopping calibration");
                    calibrationInProgress = false;
                    waypoint              = 0;
                    criticalCounter       = 0;
                    return false;
                }

                return false;
            }
            //if they are, take the average and record it to the calibration data array
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    sum += measurements[i][j];
                }
                avg                           = sum / 4;
                calibration_data[i][waypoint] = avg;
                sum                           = 0;
                criticalCounter               = 0;
            }
            log_info("Measured waypoint " << waypoint);

            //A check to see if the results on the first point are within the expected range
            if(waypoint == 0){
                double offset = _beltEndExtension + _armLength;
                double threshold = 100;

                float diffTL = calibration_data[0][0] - offset - computeTL(0, 0, 0);
                float diffTR = calibration_data[1][0] - offset - computeTR(0, 0, 0);
                float diffBL = calibration_data[2][0] - offset - computeBL(0, 0, 0);
                float diffBR = calibration_data[3][0] - offset - computeBR(0, 0, 0);
                log_info("Center point deviation: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);

                if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                    log_error("Center point deviation over " << threshold << "mmm, your coordinate system is not accurate, adjust your frame dimensions and restart.");
                    //Should we enter an alarm state here to prevent things from going wrong?


                    String message = "";
                    //If both of the bottom belts are longer than expected then the frame is smaller than expected
                    if(diffBL > threshold && diffBR > threshold){
                        log_error("Frame size error, try entering larger frame dimensions and restart.");
                        message = "Frame size error, try entering larger frame dimensions and restart.";
                    }
                    //If both of the bottom belts are shorter than expected then the frame is larger than expected
                    else if(diffBL < -threshold && diffBR < -threshold){
                        log_error("Frame size error, try entering smaller frame dimensions and restart.");
                        message = "Frame size error, try entering smaller frame dimensions and restart.";
                    }

                
                    //Stop calibration
                    eStop(message);
                    return true; 
                }
            }

            return true;
        }
    }

    return false;
}

// Move pulling just two belts depending in the direction of the movement
bool Maslow_::move_with_slack(double fromX, double fromY, double toX, double toY) {
    //This is where we want to introduce some slack so the system
    static unsigned long moveBeginTimer = millis();
    static bool          decompress     = true;
    const float          stepSize       = 0.06;
   
    static int direction = UP;


    bool withSlack = true;
    if(waypoint > recomputePoints[0]){ //If we have completed the first level of calibraiton
        withSlack = false;
    }

    //We only want to decompress at the beginning of each move
    if (decompress) {
        moveBeginTimer = millis();
        decompress = false;
        direction = get_direction(fromX, fromY, toX, toY);
        checkValidMove(fromX, fromY, toX, toY);
    }

    //Decompress belts for 500ms...this happens by returning right away before running any of the rest of the code
    if (millis() - moveBeginTimer < 750 && withSlack) {
        if (orientation == VERTICAL) {
            axisTL.recomputePID();
            axisTR.recomputePID();
            axisBL.decompressBelt();
            axisBR.decompressBelt();
        } else {
            switch (direction) {
                case UP:
                    axisBL.decompressBelt();
                    axisBR.decompressBelt();
                    break;
                case DOWN:
                    axisTL.decompressBelt();
                    axisTR.decompressBelt();
                    break;
                case LEFT:
                    axisTR.decompressBelt();
                    axisBR.decompressBelt();
                    break;
                case RIGHT:
                    axisTL.decompressBelt();
                    axisBL.decompressBelt();
                    break;
            }
        }

        return false;
    }

    //Stop for 50ms
    //we need to stop motors after decompression was finished once
    else if (millis() - moveBeginTimer < 800) {
        stopMotors();
        return false;
    }
    if(orientation == VERTICAL){
        axisTL.recomputePID();
        axisTR.recomputePID();
        if(withSlack){
            axisBL.comply();
            axisBR.comply();
        }
        else{
            axisBL.recomputePID();
            axisBR.recomputePID();
        }
    }
    else{
        switch (direction) {
            case UP:
                axisTL.recomputePID();
                axisTR.recomputePID();
                if(withSlack){
                    axisBL.comply();
                    axisBR.comply();
                }
                else{
                    axisBL.recomputePID();
                    axisBR.recomputePID();
                }
                break;
            case DOWN:
                if(withSlack){
                    axisTL.comply();
                    axisTR.comply();
                }
                else{
                    axisTL.recomputePID();
                    axisTR.recomputePID();
                }
                axisBL.recomputePID();
                axisBR.recomputePID();
                break;
            case LEFT:
                axisTL.recomputePID();
                axisBL.recomputePID();
                if(withSlack){
                    axisTR.comply();
                    axisBR.comply();
                }
                else{
                    axisTR.recomputePID();
                    axisBR.recomputePID();
                }
                break;
            case RIGHT:
                axisTR.recomputePID();
                axisBR.recomputePID();
                if(withSlack){
                    axisTL.comply();
                    axisBL.comply();
                }
                else{
                    axisTL.recomputePID();
                    axisBL.recomputePID();
                }
                break;
        }
    }

    //This system of setting the final target and then waiting until we get there doesn't feel good to me
    switch (direction) {
        case UP:
            setTargets(toX, getTargetY() + stepSize, 0);
            if (getTargetY() > toY) {
                stopMotors();
                reset_all_axis();
                decompress = true;  //Reset for the next pass
                return true;
            }
            break;
        case DOWN:
            setTargets(toX, getTargetY() - stepSize, 0);
            if (getTargetY() < toY) {
                stopMotors();
                reset_all_axis();
                decompress = true;  //Reset for the next pass
                return true;
            }
            break;
        case LEFT:
            setTargets(getTargetX() - stepSize, toY, 0);
            if (getTargetX() < toX){
                stopMotors();
                reset_all_axis();
                decompress = true;  //Reset for the next pass
                return true;
            }
            break;
        case RIGHT:
            setTargets(getTargetX() + stepSize, toY, 0);
            if (getTargetX() > toX){
                stopMotors();
                reset_all_axis();
                decompress = true;  //Reset for the next pass
                return true;
            }
            break;
    }
    return false;  //We have not yet reached our target position
}

// Direction from maslow current coordinates to the target coordinates
int Maslow_::get_direction(double x, double y, double targetX, double targetY) {
    int direction = UP;

    if (targetX - x > 1) {
        direction = RIGHT;
    } else if (targetX - x < -1) {
        direction = LEFT;
    } else if (targetY - y > 1) {
        direction = UP;
    } else if (targetY - y < -1) {
        direction = DOWN;
    }

    return direction;
}

bool Maslow_::checkValidMove(double fromX, double fromY, double toX, double toY){
    bool valid = true;
    int direction = get_direction(fromX, fromY, toX, toY);
    switch(direction){
        case UP: //If we are moving up we expect the top belts to get shorter so to should be shorter than they are now
            if(computeTL(toX, toY, 0) > axisTL.getPosition() || computeTR(toX, toY, 0) > axisTR.getPosition()){
                valid = false;
            }
            break;
        case DOWN: //If we are moving down we expect the bottom belts to get shorter so they should be shorter than they are now
            if(computeBL(toX, toY, 0) > axisBL.getPosition() || computeBR(toX, toY, 0) > axisBR.getPosition()){
                valid = false;
            }
            break;
        case LEFT: //If we are moving left we expect the left belts to get shorter so they should be shorter than they are now
            if(computeTL(toX, toY, 0) > axisTL.getPosition() || computeBL(toX, toY, 0) > axisBL.getPosition()){
                valid = false;
            }
            break;
        case RIGHT: //If we are moving right we expect the right belts to get shorter so they should be shorter than they are now
            if(computeTR(toX, toY, 0) > axisTR.getPosition() || computeBR(toX, toY, 0) > axisBR.getPosition()){
                valid = false;
            }
            break;
    }
    if(!valid){
        log_error("Unable to move safely, stopping calibration");
        calibrationInProgress = false;
        waypoint              = 0;
        eStop("Unable to move safely, stopping calibration");
    }
    return valid;
}

//The number of points high and wide  must be an odd number
bool Maslow_::generate_calibration_grid() {

    float xSpacing = calibration_grid_width_mm_X / (calibrationGridSize - 1);
    float ySpacing = calibration_grid_height_mm_Y / (calibrationGridSize - 1);

    int numberOfCycles = 0;

    switch(calibrationGridSize) {
        case 3:
            numberOfCycles = 1; // 3x3 grid
            break;
        case 5:
            numberOfCycles = 2; // 5x5 grid
            break;
        case 7:
            numberOfCycles = 3; // 7x7 grid
            break;
        case 9:
            numberOfCycles = 4; // 9x9 grid
            break;
        default:
            log_error("Invalid maslow_calibration_grid_size: " << calibrationGridSize);
            return false; // return false or handle error appropriately
    }

    pointCount = 0;

    //The point in the center
    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = 0;
    pointCount++;

    int maxX = 1;
    int maxY = 1;

    int currentX = 0;
    int currentY = -1;

    recomputeCount = 0;


    while(maxX <= numberOfCycles){ //4 produces a 9x9 grid
        while(currentX > -1*maxX){
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentX--;
        }
        while(currentY < maxY){
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentY++;
        }
        while(currentX < maxX){
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentX++;
        }
        while(currentY > -1*maxY){
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentY--;
        }

        //Add the last point to the recompute list
        calibrationGrid[pointCount][0] = currentX * xSpacing;
        calibrationGrid[pointCount][1] = currentY * ySpacing;
        pointCount++;

        recomputePoints[recomputeCount] = pointCount - 1; //Minus one because we increment after each point is generated
        recomputeCount++;

        maxX = maxX + 1;
        maxY = maxY + 1;

        currentY = currentY + -1;
    }

    //Move back to the center
    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = (currentY+1) * ySpacing; //The last loop added an nunecessary -1 to the y position
    pointCount++;

    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = 0;

    recomputePoints[recomputeCount] = pointCount;

    return true;
}

//Print calibration grid
void Maslow_::printCalibrationGrid() {
    for (int i = 0; i <= pointCount; i++) {
        log_info("Point " << i << ": " << calibrationGrid[i][0] << ", " << calibrationGrid[i][1]);
    }
    log_info("Max value for pointCount: " << pointCount);

    for(int i = 0; i < recomputeCount; i++){
        log_info("Recompute point: " << recomputePoints[i]);
    }

    log_info("Times to recompute: " << recomputeCount);
    

}

//------------------------------------------------------
//------------------------------------------------------ User commands
//------------------------------------------------------

void Maslow_::retractTL() {
    //We allow other bells retracting to continue
    retractingTL = true;
    complyALL    = false;
    extendingALL = false;
    axisTL.reset();
}
void Maslow_::retractTR() {
    retractingTR = true;
    complyALL    = false;
    extendingALL = false;
    axisTR.reset();
}
void Maslow_::retractBL() {
    retractingBL = true;
    complyALL    = false;
    extendingALL = false;
    axisBL.reset();
}
void Maslow_::retractBR() {
    retractingBR = true;
    complyALL    = false;
    extendingALL = false;
    axisBR.reset();
}
void Maslow_::retractALL() {
    retractingTL = true;
    retractingTR = true;
    retractingBL = true;
    retractingBR = true;
    complyALL    = false;
    extendingALL = false;
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}
void Maslow_::extendALL() {

    if (!all_axis_homed()) {
        log_error("Please press Retract All before using Extend All");  //I keep getting everything set up for calibration and then this trips me up
        sys.set_state(State::Idle);
        return;
    }

    stop();
    extendingALL = true;

    updateCenterXY();

    //extendCallTimer = millis();
}
void Maslow_::runCalibration() {
    if(!generate_calibration_grid()){
        return;
    }
    stop();

    //if not all axis are homed, we can't run calibration, OR if the user hasnt entered width and height?
    if (!allAxisExtended()) {
        log_error("Cannot run calibration until all axis are extended fully");
        sys.set_state(State::Idle);
        return;
    }

    sys.set_state(State::Homing);

    calibrationInProgress = true;
}
void Maslow_::comply() {
    complyCallTimer = millis();
    retractingTL    = false;
    retractingTR    = false;
    retractingBL    = false;
    retractingBR    = false;
    extendingALL    = false;
    complyALL       = true;
    axisTL.reset(); //This just resets the thresholds for pull tight
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}


//These are used to force one motor to rotate
void Maslow_::TLI(){
    TLIOveride = true;
    overideTimer = millis();
}
void Maslow_::TRI(){
    TRIOveride = true;
    overideTimer = millis();
}
void Maslow_::BLI(){
    BLIOveride = true;
    overideTimer = millis();
}
void Maslow_::BRI(){
    BRIOveride = true;
    log_info("BRI in Maslow seen");
    overideTimer = millis();
}
void Maslow_::TLO(){
    TLOOveride = true;
    overideTimer = millis();
}
void Maslow_::TRO(){
    TROOveride = true;
    overideTimer = millis();
}
void Maslow_::BLO(){
    BLOOveride = true;
    overideTimer = millis();
}
void Maslow_::BRO(){
    BROOveride = true;
    overideTimer = millis();
}
void Maslow_::handleMotorOverides(){
    if(TLIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisTL.fullIn();
        }else{
            TLIOveride = false;
            axisTL.stop();
        }
    }
    if(BRIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisBR.fullIn();
        }else{
            BRIOveride = false;
            axisBR.stop();
        }
    }
    if(TRIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisTR.fullIn();
        }else{
            TRIOveride = false;
            axisTR.stop();
        }
    }
    if(BLIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisBL.fullIn();
        }else{
            BLIOveride = false;
            axisBL.stop();
        }
    }
    if(TLOOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisTL.fullOut();
        }else{
            TLOOveride = false;
            axisTL.stop();
        }
    }
    if(BROOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisBR.fullOut();
        }else{
            BROOveride = false;
            axisBR.stop();
        }
    }
    if(TROOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisTR.fullOut();
        }else{
            TROOveride = false;
            axisTR.stop();
        }
    }
    if(BLOOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            axisBL.fullOut();
        }else{
            BLOOveride = false;
            axisBL.stop();
        }
    }
}

bool Maslow_::checkOverides(){
    if(TLIOveride || TRIOveride || BLIOveride || BRIOveride || TLOOveride || TROOveride || BLOOveride || BROOveride){
        return true;
    }
    return false;
}

void Maslow_::setSafety(bool state) {
    safetyOn = state;
}
void Maslow_::test_() {
    log_info("Firmware Version: " << VERSION_NUMBER);

    axisTL.test();
    axisTR.test();
    axisBL.test();
    axisBR.test();

}
void Maslow_::set_frame_width(double width) {
    trX = width;
    brX = width;
    updateCenterXY();
}
void Maslow_::set_frame_height(double height) {
    tlY = height;
    trY = height;
    updateCenterXY();
}
void Maslow_::take_slack() {
    //if not all axis are homed, we can't take the slack up
    if (!allAxisExtended()) {
        log_error("Cannot take slack until all axis are extended fully");
        sys.set_state(State::Idle);
        return;
    }
    retractingTL = false;
    retractingTR = false;
    retractingBL = false;
    retractingBR = false;
    extendingALL = false;
    complyALL    = false;
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();

    x         = 0;
    y         = 0;
    takeSlack = true;
}

//------------------------------------------------------
//------------------------------------------------------ Utility functions
//------------------------------------------------------

//non-blocking delay, just pauses everything for specified time
void Maslow_::hold(unsigned long time) {
    holdTime  = time;
    holding   = true;
    holdTimer = millis();
}

// Resets variables on all 4 axis
void Maslow_::reset_all_axis() {
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}

// True if all axis were zeroed
bool Maslow_::all_axis_homed() {
    return axis_homed[0] && axis_homed[1] && axis_homed[2] && axis_homed[3];
}

// True if all axis were extended
bool Maslow_::allAxisExtended() {
    return extendedTL && extendedTR && extendedBL && extendedBR;
}

// int to string name conversion for axis labels
String Maslow_::axis_id_to_label(int axis_id) {
    String label;
    switch (axis_id) {
        case TLEncoderLine:
            label = "Top Left";
            break;
        case TREncoderLine:
            label = "Top Right";
            break;
        case BREncoderLine:
            label = "Bottom Right";
            break;
        case BLEncoderLine:
            label = "Bottom Left";
            break;
    }
    return label;
}

//Checks to see if the calibration data needs to be sent again
void Maslow_::checkCalibrationData() {
    if (calibrationDataWaiting > 0) {
        if (millis() - calibrationDataWaiting > 30007) {
            log_error("Calibration data not acknowledged by computer, resending");
            print_calibration_data();
            calibrationDataWaiting = millis();
        }
    }
}

// function for outputting calibration data in the log line by line like this: {bl:2376.69,   br:923.40,   tr:1733.87,   tl:2801.87},
void Maslow_::print_calibration_data() {
    String data = "CLBM:[";
    for (int i = 0; i < waypoint; i++) {
        data += "{bl:" + String(calibration_data[2][i]) + ",   br:" + String(calibration_data[3][i]) +
                ",   tr:" + String(calibration_data[1][i]) + ",   tl:" + String(calibration_data[0][i]) + "},";
    }
    data += "]";
    HeartBeatEnabled = false;
    log_data(data.c_str());  //will it print really large strings?
    HeartBeatEnabled = true;
}

//Runs when the calibration data has been acknowledged as received by the computer and the calibration process is progressing
void Maslow_::calibrationDataRecieved(){
    // log_info("Calibration data acknowledged received by computer");
    calibrationDataWaiting = -1;
}

// Stop all motors and reset all state variables
void Maslow_::stop() {
    stopMotors();
    retractingTL          = false;
    retractingTR          = false;
    retractingBL          = false;
    retractingBR          = false;
    extendingALL          = false;
    complyALL             = false;
    calibrationInProgress = false;
    test                  = false;
    takeSlack             = false;

    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}

// Stop all the motors
void Maslow_::stopMotors() {
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
}

// Panic function, stops all motors and sets state to alarm
void Maslow_::panic() {
    log_error("PANIC! Stopping all motors");
    stop();
    sys.set_state(State::Alarm);
}

//Emergecy Stop
void Maslow_::eStop(String message) {
    log_error("Emergency stop! Stopping all motors");
    log_warn("The machine will not respond until turned off and back on again");
    stop();
    error = true;
    errorMessage = message;
    sys.set_state(State::Alarm);
}

// Get's the most recently set target position in X
double Maslow_::getTargetX() {
    return targetX;
}

// Get's the most recently set target position in Y
double Maslow_::getTargetY() {
    return targetY;
}

//Get's the most recently set target position in Z
double Maslow_::getTargetZ() {
    return targetZ;
}

//Updates where the center x and y positions are
void Maslow_::updateCenterXY() {
    double A = (trY - blY) / (trX - blX);
    double B = (brY - tlY) / (brX - tlX);
    centerX  = (brY - (B * brX) + (A * trX) - trY) / (A - B);
    centerY  = A * (centerX - trX) + trY;
}

Maslow_& Maslow_::getInstance() {
    static Maslow_ instance;
    return instance;
}

Maslow_& Maslow = Maslow.getInstance();
