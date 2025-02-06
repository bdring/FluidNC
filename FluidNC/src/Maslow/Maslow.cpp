// Copyright (c) 2014 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "Maslow.h"
#include "../Report.h"
#include "../WebUI/WifiConfig.h"
#include "../Protocol.h"
#include "../System.h"
#include "../FileStream.h"

// Maslow specific defines
#define VERSION_NUMBER "0.88"

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
    Wire.begin(5, 4, 200000);
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

    loadZPos(); //Loads the z-axis position from EEPROM

    stopMotors();

    Wire.setTimeOut(10);

    if (error) {
        log_error(M+" failed to initialize - fix errors and restart");
    } else {
        log_info("Starting "+M+" Version " << VERSION_NUMBER);
    }
}

// Maslow main loop, everything is processed here
void Maslow_::update() {
    static State prevState = sys.state();

    //If we are in an error state, blink the LED and stop the motors
    if (error) {
        static unsigned long timer = millis();
        static bool          st    = true; //This is used to blink the LED
        if (millis() - timer > 300) {
            stopMotors();
            st = !st;
            digitalWrite(REDLED, st);
            timer = millis();
            if (errorMessage != "") {
                log_error(errorMessage.c_str());
            }
            errorMessage = "";
        }
        return;
    }

    //Blinks the Ethernet LEDs randomly...these have been removed from the board, this pin should be freed up
    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, LOW);
    }
    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, HIGH);
    }

    //Save the z-axis position if the prevous state was jog or cycle and the current state is idle
    if ((prevState == State::Jog || prevState == State::Cycle) && sys.state() == State::Idle) {
        saveZPos();
    }

    blinkIPAddress();

    // The ui is using the ping from the websocket to detect issues. we don't need the info message any more.
    // heartBeat();

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

        //temp test function...This is used for debugging when the test command is sent
        if (test) {
            test = false;
        }

        //------------------------ Maslow State Machine

        //-------Jog or G-code execution.
        if (sys.state() == State::Jog || sys.state() == State::Cycle) {
            Maslow.setTargets(steps_to_mpos(get_axis_motor_steps(0), 0),
                              steps_to_mpos(get_axis_motor_steps(1), 1),
                              steps_to_mpos(get_axis_motor_steps(2), 2));

            //This disables the belt motors until the user has completed calibration or apply tension and they have succeded
            if (setupComplete()) {
                Maslow.recomputePID();
            }
        }
        //--------Homing routines
        else if (sys.state() == State::Homing) {
            home();
        } else {  //This is confusing to understand. This is an else if so this is only run if we are not in jog, cycle, or homing
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

    prevState = sys.state(); //Store for next time
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

//Sends a heartbeat message to the UI...should be replaced with the built in ones for fluidNC
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
                extendedTL = axisTL.extend(extendDist);
            if (!extendedTR)
                extendedTR = axisTR.extend(extendDist);
            if (!extendedBL)
                extendedBL = axisBL.extend(extendDist);
            if (!extendedBR)
                extendedBR = axisBR.extend(extendDist);
            if (extendedTL && extendedTR && extendedBL && extendedBR) {
                extendingALL = false;
                log_info("All belts extended to " << extendDist << "mm");
            }
        }
    }
    //  - comply mode
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
            axisTL.stop();
            axisTR.stop();
            axisBL.stop();
            axisBR.stop();
            complyALL = false;
            sys.set_state(State::Idle);
            setupIsComplete = false; //We've undone the setup so apply tension is needed before we can move
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
            deallocateCalibrationMemory();
        }
    }

    handleMotorOverides();

    //if we are done with all the homing moves, switch system state back to Idle?
    if (!retractingTL && !retractingBL && !retractingBR && !retractingTR && !extendingALL && !complyALL && !calibrationInProgress &&
        !takeSlack && !checkOverides()) {
        sys.set_state(State::Idle);
    }
}

/*
* This function is used to take up the slack in the belts and confirm that the calibration values are resonable
* It is run when the "Apply Tension" button is pressed in the UI
* It does this by retracting the two lower belts and taking a measurement. The machine's position is then calculated 
* from the lenghts of the two upper belts. The lengths of the two lower belts are then compared to their expected calculated lengths
* If the difference is beyond a threshold we know that the stored anchor point locations do not match the real dimensons and and error is thrown
*/
bool Maslow_::takeSlackFunc() {
    static int takeSlackState = 0; //0 -> Starting, 1-> Moving to (0,0), 2-> Taking a measurement
    static unsigned long holdTimer = millis();
    static float startingX    = 0;
    static float startingY    = 0;

    //Take a measurement
    if(takeSlackState == 0){
        if (take_measurement_avg_with_check(2, UP)) { //We really shouldn't be using the second position to store the data, it should have it's own array
            
            float x = 0;
            float y = 0;
            if(!computeXYfromLengths(calibration_data[2][0], calibration_data[2][1], x, y)){
                log_error("Failed to compute XY from lengths");
                return true;
            }

            float extension = _beltEndExtension + _armLength;
            
            //This should use it's own array, this is not calibration data
            float diffTL = calibration_data[2][0] - measurementToXYPlane(computeTL(x, y, 0), tlZ);
            float diffTR = calibration_data[2][1] - measurementToXYPlane(computeTR(x, y, 0), trZ);
            float diffBL = calibration_data[2][2] - measurementToXYPlane(computeBL(x, y, 0), blZ);
            float diffBR = calibration_data[2][3] - measurementToXYPlane(computeBR(x, y, 0), brZ);
            log_info("Center point deviation: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);
            double threshold = 12;
            if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                log_error("Center point deviation over " << threshold << "mm, your coordinate system is not accurate, maybe try running calibration again?");
                //Should we enter an alarm state here to prevent things from going wrong?

                //Reset
                takeSlackState = 0;
                return true;
            }
            else{
                log_info("Center point deviation within " << threshold << "mm, your coordinate system is accurate");
                takeSlackState = 0;
                holdTimer = millis();
                setupIsComplete = true;

                log_info("Current machine position loaded as X: " << x << " Y: " << y );

                float* mpos = get_mpos();
                mpos[0] = x;
                mpos[1] = y;
                set_motor_steps_from_mpos(mpos);
                gc_sync_position();//This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
                plan_sync_position();

                sys.set_state(State::Idle);
            }
        }
    }

    //Position hold for 2 seconds
    if(takeSlackState == 1){
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
    static bool measurementInProgress = true; //We start by taking a measurement, then we move
    if(waypoint > pointCount){ //Point count is the total number of points to measure so if waypoint > pointcount then the overall measurement process is complete
        calibrationInProgress = false;
        waypoint              = 0;
        setupIsComplete       = true;
        log_info("Calibration complete");
        deallocateCalibrationMemory();
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
                                      calibrationGrid[waypoint][1]); //This is used to set the order that the belts are pulled tight in the following measurement
            x                     = calibrationGrid[waypoint][0]; //Are these ever used anywhere?
            y                     = calibrationGrid[waypoint][1];
            hold(250);
        }
    }
}

// Function to allocate memory for calibration arrays
void Maslow_::allocateCalibrationMemory() {
    if(calibrationGrid == nullptr){ //Check to prevent realocating
        calibrationGrid = new float[CALIBRATION_GRID_SIZE_MAX][2];
    }
    if(calibration_data == nullptr){
        calibration_data = new float*[CALIBRATION_GRID_SIZE_MAX];
        for (int i = 0; i < CALIBRATION_GRID_SIZE_MAX; ++i) {
            calibration_data[i] = new float[4];
        }
    }
}

// Function to deallocate memory for calibration arrays
void Maslow_::deallocateCalibrationMemory() {
    delete[] calibrationGrid;
    calibrationGrid = nullptr;
    for (int i = 0; i < CALIBRATION_GRID_SIZE_MAX; ++i) {
        delete[] calibration_data[i];
        }
    delete[] calibration_data;
    calibration_data = nullptr;
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
    static int           tresholdHitsBeforePanic = 150;
    static int           panicCounter[4]         = { 0 };

    static int           positionErrorCounter[4] = { 0 };
    static float         previousPositionError[4] = { 0, 0, 0, 0 };

    MotorUnit* axis[4] = { &axisTL, &axisTR, &axisBL, &axisBR };
    for (int i = 0; i < 4; i++) {
        //If the current exceeds some absolute value, we need to call panic() and stop the machine
        if (axis[i]->getMotorCurrent() > 4000 && !tick[i]) {
            panicCounter[i]++;
            if (panicCounter[i] > tresholdHitsBeforePanic) {
                if(sys.state() == State::Jog || sys.state() == State::Cycle){
                    log_warn("Motor current on " << axis_id_to_label(i).c_str() << " axis exceeded threshold of " << 4000);
                    //Maslow.panic();
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

        //If the motor has a position error greater than 15mm and we are running a file or jogging
        previousPositionError[i] = axis[i]->getPositionError();
        if ((abs(axis[i]->getPositionError()) > 15) && (sys.state() == State::Cycle)) {
            positionErrorCounter[i]++;
            log_warn("Position error on " << axis_id_to_label(i).c_str() << " axis exceeded 15mm while running. Error is "
                                            << axis[i]->getPositionError() << "mm" << " Counter: " << positionErrorCounter[i]);
            log_warn("Previous error was " << previousPositionError[i] << "mm");

            if(positionErrorCounter[i] > 5){
                Maslow.eStop("Position error > 15mm while running. E-Stop triggered.");
            }
        }
        else{
            positionErrorCounter[i] = 0;
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
    float a = blX - x; //X dist from corner to router center
    float b = blY - y; //Y dist from corner to router center
    float c = 0.0 - (z + blZ); //Z dist from corner to router center

    float XYlength = sqrt(a * a + b * b); //Get the distance in the XY plane from the corner to the router center

    float XYBeltLength = XYlength - (_beltEndExtension + _armLength); //Subtract the belt end extension and arm length to get the belt length

    float length = sqrt(XYBeltLength * XYBeltLength + c * c); //Get the angled belt length

    return length;
}
float Maslow_::computeBR(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = brX - x;
    float b = brY - y;
    float c = 0.0 - (z + brZ);

    float XYlength = sqrt(a * a + b * b); //Get the distance in the XY plane from the corner to the router center

    float XYBeltLength = XYlength - (_beltEndExtension + _armLength); //Subtract the belt end extension and arm length to get the belt length

    float length = sqrt(XYBeltLength * XYBeltLength + c * c); //Get the angled belt length

    return length;
}
float Maslow_::computeTR(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = trX - x;
    float b = trY - y;
    float c = 0.0 - (z + trZ);
    
    float XYlength = sqrt(a * a + b * b); //Get the distance in the XY plane from the corner to the router center

    float XYBeltLength = XYlength - (_beltEndExtension + _armLength); //Subtract the belt end extension and arm length to get the belt length

    float length = sqrt(XYBeltLength * XYBeltLength + c * c); //Get the angled belt length

    return length;
}
float Maslow_::computeTL(float x, float y, float z) {
    //Move from lower left corner coordinates to centered coordinates
    x       = x + centerX;
    y       = y + centerY;
    float a = tlX - x;
    float b = tlY - y;
    float c = 0.0 - (z + tlZ);
    
    float XYlength = sqrt(a * a + b * b); //Get the distance in the XY plane from the corner to the router center

    float XYBeltLength = XYlength - (_beltEndExtension + _armLength); //Subtract the belt end extension and arm length to get the belt length

    float length = sqrt(XYBeltLength * XYBeltLength + c * c); //Get the angled belt length

    return length;
}

//------------------------------------------------------
//------------------------------------------------------ Homing and calibration functions
//------------------------------------------------------

//Takes a raw measurement, projects it into the XY plane, then adds the belt end extension and arm length to get the actual distance.
float Maslow_::measurementToXYPlane(float measurement, float zHeight){

    float lengthInXY = sqrt(measurement * measurement - zHeight * zHeight);
    return lengthInXY + _beltEndExtension + _armLength; //Add the belt end extension and arm length to get the actual distance
}

/*
*Computes the current xy cordinates of the sled based on the lengths of the upper two belts
*/
bool Maslow_::computeXYfromLengths(double TL, double TR, float &x, float &y) {
    double tlLength = TL;//measurementToXYPlane(TL, tlZ);
    double trLength = TR;//measurementToXYPlane(TR, trZ);

    //Find the intersection of the two circles centered at tlX, tlY and trX, trY with radii tlLength and trLength
    double d = sqrt((tlX - trX) * (tlX - trX) + (tlY - trY) * (tlY - trY));
    if (d > tlLength + trLength || d < abs(tlLength - trLength)) {
        log_info("Unable to determine machine position");
        return false;
    }
    
    double a = (tlLength * tlLength - trLength * trLength + d * d) / (2 * d);
    double h = sqrt(tlLength * tlLength - a * a);
    double x0 = tlX + a * (trX - tlX) / d;
    double y0 = tlY + a * (trY - tlY) / d;
    double rawX = x0 + h * (trY - tlY) / d;
    double rawY = y0 - h * (trX - tlX) / d;

    // Adjust to the centered coordinates
    x = rawX - centerX;
    y = rawY - centerY;

    return true;
}

/**
 * Takes one measurement and returns true when it's done. The result is stored in the passed array.
 * Each measurement is the raw belt length processed into XY plane coordinates.
 * 
 * The function handles two orientations: VERTICAL and HORIZONTAL.
 * 
 * In VERTICAL orientation:
 * - Pulls two bottom belts tight one after another based on the x-coordinate.
 * - Takes a measurement once both belts are tight and stores it in the calibration data array.
 * 
 * In HORIZONTAL orientation:
 * - Pulls belts tight based on the direction of the last move.
 * - Takes a measurement once both belts are tight and stores it in the calibration data array.
 * 
 * @param waypoint The waypoint number to store the result.
 * @param dir The direction of the last move (UP, DOWN, LEFT, RIGHT). This is used to descide which belts to tighten first
 * @param run The run mode (0 for sequential tightening, non-zero for simultaneous tightening).
 * @return True when the measurement is done, false otherwise.
 */
bool Maslow_::take_measurement(float result[4], int dir, int run, int current) {

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
                if (axisBL.pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
            if (!BR_tight) {
                if (axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
        }

        //On the right side of the sheet we want to pull the right belt tight first
        else {
            if (!BR_tight) {
                if (axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
            if (!BL_tight) {
                if (axisBL.pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
        }

        //once both belts are pulled, take a measurement
        if (BR_tight && BL_tight) {
            //take measurement and record it to the calibration data array.
            result[0] = measurementToXYPlane(axisTL.getPosition(), tlZ);
            result[1] = measurementToXYPlane(axisTR.getPosition(), trZ);
            result[2] = measurementToXYPlane(axisBL.getPosition(), blZ);
            result[3] = measurementToXYPlane(axisBR.getPosition(), brZ);
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
                if (pullAxis1->pull_tight(current)) {
                    pull1_tight      = true;
                }
                if (run == 0) //Second axis complies while first is pulling
                    pullAxis2->comply();
                return false;
            }
            if (!pull2_tight) {
                if (pullAxis2->pull_tight(current)) {
                    pull2_tight      = true;
                }
                return false;
            }
        }
        else{
            if (pullAxis1->pull_tight(current)) {
                pull1_tight      = true;
            }
            if (pullAxis2->pull_tight(current)) {
                pull2_tight      = true;
            }
        }
        if (pull1_tight && pull2_tight) {
            //take measurement and record it to the calibration data array.
            result[0] = measurementToXYPlane(axisTL.getPosition(), tlZ);
            result[1] = measurementToXYPlane(axisTR.getPosition(), trZ);
            result[2] = measurementToXYPlane(axisBL.getPosition(), blZ);
            result[3] = measurementToXYPlane(axisBR.getPosition(), brZ);
            pull1_tight                   = false;
            pull2_tight                   = false;
            return true;
        }
    }

    return false;
}

static float** measurements = nullptr;

void allocateMeasurements() {
    measurements = new float*[4];
    for (int i = 0; i < 4; ++i) {
        measurements[i] = new float[4];
    }
}

void freeMeasurements() {
    for (int i = 0; i < 4; ++i) {
        delete[] measurements[i];
    }
    delete[] measurements;
    measurements = nullptr;
}

// Takes a series of measurements, calculates average and records calibration data;  Returns true when it's done and the result has been stored
// There is way too much being done in this function. It needs to be split apart and cleaned up
bool Maslow_::take_measurement_avg_with_check(int waypoint, int dir) {
    //take 5 measurements in a row, (ignoring the first one), if they are all within 1mm of each other, take the average and record it to the calibration data array
    static int           run                = 0;
    static float         avg                = 0;
    static float         sum                = 0;
    static bool          measureFlex        = false;

    if (measurements == nullptr) {
        allocateMeasurements(); //This is structured [[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]]
    }


    int howHardToPull = calibrationCurrentThreshold;
    if(measureFlex){
        howHardToPull = calibrationCurrentThreshold + 500;
    }

    if (take_measurement(measurements[max(run-2, 0)], dir, run, howHardToPull)) { //Throw away measurements are stored in [0]
        if (run < 2) {
            run++;
            return false;  //discard the first two measurements
        }

        run++;

        static int criticalCounter = 0;
        if (run > 5) {
            run = 0;

            //check if all measurements are within 1mm of each other
            float maxDeviation[4] = { 0 };
            float maxDeviationAbs = 0;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 3; j++) {
                    //find max deviation between measurements
                    maxDeviation[i] = max(maxDeviation[i], abs(measurements[j][i] - measurements[j+1][i]));
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
                        log_info(axis_id_to_label(i).c_str() << " " << measurements[j][i]);
                    }
                }
                //reset the run counter to run the measurements again
                if (criticalCounter++ > 8) { //This updates the counter and checks
                    log_error("Critical error, measurements are not within 1.5mm of each other 8 times in a row, stopping calibration");
                    calibrationInProgress = false;
                    waypoint              = 0;
                    criticalCounter       = 0;
                    freeMeasurements();
                    return false;
                }
                freeMeasurements();
                return false;
            }

            //If we are measurring the flex we don't want to save the result and instead we want to compare it to the last result
            if(measureFlex){
                float newLenTLBR = measurements[0][0] + measurements[0][3];
                float newLenTRBL = measurements[0][1] + measurements[0][2];

                float origLenTLBR = calibration_data[0][0] + calibration_data[0][3];
                float origLenTRBL = calibration_data[0][1] + calibration_data[0][2];

                float diffTLBR = abs(newLenTLBR - origLenTLBR);
                float diffTRBL = abs(newLenTRBL - origLenTRBL);

                log_info("Flex measurement: TLBR: " << diffTLBR << " TRBL: " << diffTRBL);

                measureFlex = false;

                freeMeasurements(); //We have completed this measurement, but we don't want to store anything this time
                return true;
            }

            //If the measurements seem valid, take the average and record it to the calibration data array. This is the only place we should be writing to the calibration_data array
            for (int i = 0; i < 4; i++) { //For each axis
                sum = measurements[0][i] + measurements[1][i] + measurements[2][i] + measurements[3][i];
                avg                           = sum / 4;
                calibration_data[waypoint][i] = avg; //This is the only time we should be writing to the calibration data array
                sum                           = 0;
                criticalCounter               = 0;
            }
            log_info("Measured waypoint " << waypoint);

            //A check to see if the results on the first point are within the expected range
            if(waypoint == 0){

                //Recompute the machine position with the belt lenths and compare the results to that
                float x = 0;
                float y = 0;
                computeXYfromLengths(measurements[0][0], measurements[0][1], x, y);

                //If the frame size is way off, we will compute a rough (assumed to be a square) frame size from the first measurmeent
                double threshold = 100;
                float diffTL = measurements[0][0] - measurementToXYPlane(computeTL(x, y, 0), tlZ);
                float diffTR = measurements[0][1] - measurementToXYPlane(computeTR(x, y, 0), trZ);
                float diffBL = measurements[0][2] - measurementToXYPlane(computeBL(x, y, 0), blZ);
                float diffBR = measurements[0][3] - measurementToXYPlane(computeBR(x, y, 0), brZ);
                log_info("Center point off by: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);

                if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                    log_error("Center point off by over " << threshold << "mm");

                    if(!adjustFrameSizeToMatchFirstMeasurement()){
                        eStop("Unable to find a valid frame size to match the first measurement");
                        calibrationInProgress = false;
                        waypoint              = 0;
                        criticalCounter       = 0;
                        freeMeasurements();
                        return false;
                    }
                }

                //Compute the current XY position from the top two belt measurements...needs to be redone because we've adjusted the frame size by here
                if(!computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y)){
                    eStop("Unable to find machine position from measurements");
                    calibrationInProgress = false;
                    waypoint              = 0;
                    criticalCounter       = 0;
                    freeMeasurements();
                    return false;
                }

                log_info("Machine Position computed as X: " << x << " Y: " << y);

                //Recompute the first four waypoint locations based on the current position
                calibrationGrid[0][0] = x;//This first point is never really used because we've already measured here, but it shouldn't be left undefined
                calibrationGrid[0][1] = y;
                calibrationGrid[1][0] = x + 150;
                calibrationGrid[1][1] = y;
                calibrationGrid[2][0] = x + 150;
                calibrationGrid[2][1] = y + 150;
                calibrationGrid[3][0] = x;
                calibrationGrid[3][1] = y + 150;
                calibrationGrid[4][0] = x - 150;
                calibrationGrid[4][1] = y + 150;
                calibrationGrid[5][0] = x - 150;
                calibrationGrid[5][1] = y;
                
            }

            //This is the exit to indicate that the measurement was successful
            freeMeasurements();

            //Special case where we have a good measurement but we need to take another at this point to measure the flex of the frame
            if(waypoint == 0){
                measureFlex = true;
                log_info("Measuring Frame Flex");
                return false;
            }

            return true; 
        }
    }
    //We don't free memory alocated here because we will cycle through again and need it
    return false;
}

// Move pulling just two belts depending in the direction of the movement
bool Maslow_::move_with_slack(double fromX, double fromY, double toX, double toY) {
    //This is where we want to introduce some slack so the system
    static unsigned long moveBeginTimer = millis();
    static bool          decompress     = true;
    float                stepSize       = 0.06;

    static int direction = UP;

    static float xStepSize = 1;
    static float yStepSize = 1;

    static bool tlExtending = false;
    static bool trExtending = false;
    static bool blExtending = false;
    static bool brExtending = false;
    
    bool withSlack = true;
    if(waypoint > recomputePoints[0]){ //If we have completed the first round of calibraiton
        withSlack = false;
    }

    //This runs once at the beginning of the move
    if (decompress) {
        moveBeginTimer = millis();
        decompress = false;
        direction = get_direction(fromX, fromY, toX, toY);

        //Compute the X and Y step Size
        if (abs(toX - fromX) > abs(toY - fromY)) {
            xStepSize = (toX - fromX) > 0 ? stepSize : -stepSize;
            yStepSize = ((toY - fromY) > 0 ? stepSize : -stepSize) * abs(toY - fromY) / abs(toX - fromX);
        } else {
            yStepSize = (toY - fromY) > 0 ? stepSize : -stepSize;
            xStepSize = ((toX - fromX) > 0 ? stepSize : -stepSize) * abs(toX - fromX) / abs(toY - fromY);
        }

        //Compute which belts will be getting longer. If the current length is less than the final length the belt needs to get longer
        if (computeTL(fromX, fromY, 0) < computeTL(toX, toY, 0)) {
            tlExtending = true;
        } else {
            tlExtending = false;
        }
        if (computeTR(fromX, fromY, 0) < computeTR(toX, toY, 0)) {
            trExtending = true;
        } else {
            trExtending = false;
        }
        if (computeBL(fromX, fromY, 0) < computeBL(toX, toY, 0)) {
            blExtending = true;
        } else {
            blExtending = false;
        }
        if (computeBR(fromX, fromY, 0) < computeBR(toX, toY, 0)) {
            brExtending = true;
        } else {
            brExtending = false;
        }

        //Set the target to the starting position
        setTargets(fromX, fromY, 0);
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

    //Set the targets
    setTargets(getTargetX() + xStepSize, getTargetY() + yStepSize, 0);

        //Check to see if we have reached our target position
    if (abs(getTargetX() - toX) < 5 && abs(getTargetY() - toY) < 5) {
        stopMotors();
        reset_all_axis();
        decompress = true;  //Reset for the next pass
        return true;
    }

    //In vertical orientation we want to move with the top two belts and always have the lower ones be slack
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

        //For each belt we check to see if it should be slack
        if(withSlack && tlExtending){
            axisTL.comply();
        }
        else{
            axisTL.recomputePID();
        }

        if(withSlack && trExtending){
            axisTR.comply();
        }
        else{
            axisTR.recomputePID();
        }

        if(withSlack && blExtending){
            axisBL.comply();
        }
        else{
            axisBL.recomputePID();
        }

        if(withSlack && brExtending){
            axisBR.comply();
        }
        else{
            axisBR.recomputePID();
        }
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

/*
* This function takes a single measurement and adjusts the frame dimensions to find a valid frame size that matches the measurement
*/
bool Maslow_::adjustFrameSizeToMatchFirstMeasurement() {

    //Get the last measurments
    double tlLen = measurements[0][0];
    double trLen = measurements[0][1];
    double blLen = measurements[0][2];
    double brLen = measurements[0][3];

    //Check that we are in fact on the center line. The math assumes that we are roughly centered on the frame and so
    //the topleft and topright measurements should be roughly the same. It doesn't need to be exact.
    if (std::abs(tlLen - trLen) > 20) {
        log_error("Unable to adjust frame size. Not centered."); //There exists a more generalized solution which should be implimented here: https://math.stackexchange.com/questions/5013127/find-square-size-from-inscribed-triangles?noredirect=1#comment10752043_5013127
        return false;
    }

    //Compute the size of the frame from the given measurements

    double numerator = sqrt(pow(tlLen, 2) + sqrt(-pow(tlLen, 4) + 6 * pow(tlLen, 2) * pow(blLen, 2) - pow(blLen, 4)) + pow(blLen, 2));
    double denominator = sqrt(2);
    float L = numerator / denominator;

    //Adjust the frame size to match the computed size
    tlY = L;
    trX = L;
    trY = L;
    brX = L;
    updateCenterXY();

    log_info("Frame size automaticlaly adjusted to " + std::to_string(brX) + " by " + std::to_string(trY));
    return true;
}

//The number of points high and wide  must be an odd number
bool Maslow_::generate_calibration_grid() {

    //Allocate memory for the calibration grid
    allocateCalibrationMemory();

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
            log_error("Invalid "+M+"_calibration_grid_size: " << calibrationGridSize);
            return false; // return false or handle error appropriately
    }

    pointCount = 6; //The first four points are computed dynamically
    recomputePoints[0] = 5;

    //The point in the center
    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = 0;

    pointCount++;

    int maxX = 1;
    int maxY = 1;

    int currentX = 0;
    int currentY = -1;

    recomputeCount = 1;


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
// void Maslow_::printCalibrationGrid() {
//     for (int i = 0; i <= pointCount; i++) {
//         log_info("Point " << i << ": " << calibrationGrid[i][0] << ", " << calibrationGrid[i][1]);
//     }
//     log_info("Max value for pointCount: " << pointCount);

//     for(int i = 0; i < recomputeCount; i++){
//         log_info("Recompute point: " << recomputePoints[i]);
//     }

//     log_info("Times to recompute: " << recomputeCount);


// }

//------------------------------------------------------
//------------------------------------------------------ User commands
//------------------------------------------------------

// void Maslow_::retractTL() {
//     //We allow other bells retracting to continue
//     retractingTL = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisTL.reset();
// }
// void Maslow_::retractTR() {
//     retractingTR = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisTR.reset();
// }
// void Maslow_::retractBL() {
//     retractingBL = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisBL.reset();
// }
// void Maslow_::retractBR() {
//     retractingBR = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisBR.reset();
// }
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
    setupIsComplete = false;
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

/*
* This function is called once when calibration is started
*/
void Maslow_::runCalibration() {

    //If we are at the first point we need to generate the grid before we can start
    if (waypoint == 0) {
        if(!generate_calibration_grid()){ //Fail out if the grid cannot be generated
            return;
        }
    }
    stop();

    //Save the z-axis 'stop' position
    targetZ = 0;
    setZStop();

    //if not all axis are homed, we can't run calibration, OR if the user hasnt entered width and height?
    if (!allAxisExtended()) {
        log_error("Cannot run calibration until all belts are extended fully");
        sys.set_state(State::Idle);
        return;
    }

    //Recalculate the center position because the machine dimensions may have been updated
    updateCenterXY();


    //At this point it's likely that we have just sent the machine new cordinates for the anchor points so we need to figure out our new XY
    //cordinates by looking at the current lengths of the top two belts.
    //If we can't load the position, that's OK, we can still go ahead with the calibration and the first point will make a guess for it
    float x = 0;
    float y = 0;
    if(computeXYfromLengths(measurementToXYPlane(axisTL.getPosition(), tlZ), measurementToXYPlane(axisTR.getPosition(), trZ), x, y)){
        
        //We reset the last waypoint to where it actually is so that we can move from the updated position to the next waypoint
        if(waypoint > 0){
            calibrationGrid[waypoint - 1][0] = x;
            calibrationGrid[waypoint - 1][1] = y;
        }

        log_info("Machine Position found as X: " << x << " Y: " << y);

        //Set the internal machine position to the new XY position
        float* mpos = get_mpos();
        mpos[0] = x;
        mpos[1] = y;
        set_motor_steps_from_mpos(mpos);
        gc_sync_position();//This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
        plan_sync_position();
        
    }

    sys.set_state(State::Homing);

    calibrationInProgress = true;
}

//This function is used for release tension
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

/*
* This function is used to manuall force the motors to move for a fraction of a second to clear jams
*/
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

    log_info("I2C Timeout: ");
    log_info(Wire.getTimeOut());

    axisTL.test();
    axisTR.test();
    axisBL.test();
    axisBR.test();
}
//This function saves the current z-axis position to the non-volitle storage
void Maslow_::saveZPos() {
    nvs_handle_t nvsHandle;
    esp_err_t ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle!\n");
        return;
    }

    // Read the current value
    int32_t currentZPos;
    ret = nvs_get_i32(nvsHandle, "zPos", &currentZPos);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading from NVS!\n");
        return;
    }

    // Write - Convert the float to an int32_t and write only if it has changed
    union FloatInt32 {
        float f;
        int32_t i;
    };
    FloatInt32 fi;
    fi.f = targetZ;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentZPos != fi.i) { // Only write if the value has changed
        ret = nvs_set_i32(nvsHandle, "zPos", fi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing to NVS!\n");
        } else {
            //log_info("Written value = " + std::to_string(targetZ));

            // Commit written value to non-volatile storage
            ret = nvs_commit(nvsHandle);
            if (ret != ESP_OK) {
                log_info("Error " + std::string(esp_err_to_name(ret)) + " committing changes to NVS!\n");
            }
        }
    }
}

//This function loads the z-axis position from the non-volitle storage
void Maslow_::loadZPos() {
    nvs_handle_t nvsHandle;
    esp_err_t ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle!\n");
        return;
    }

    // Read
    int32_t value2;
    ret = nvs_get_i32(nvsHandle, "zPos", &value2);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading from NVS!");
    } else {
        union FloatInt32 {
            float f;
            int32_t i;
        };
        FloatInt32 fi;
        fi.i = value2;
        targetZ = fi.f;

        int zAxis = 2;
        float* mpos = get_mpos();
        mpos[zAxis] = targetZ;
        set_motor_steps_from_mpos(mpos);

        log_info("Current z-axis position loaded as: " << targetZ);

        gc_sync_position();//This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
        plan_sync_position();
    }
}

/** Sets the 'bottom' Z position, this is a 'stop' beyond which travel cannot continue */
void Maslow_::setZStop() {
    log_info("Setting z-stop position");

    targetZ = 0;

    int zAxis = 2;
    float* mpos = get_mpos();
    mpos[zAxis] = targetZ;
    set_motor_steps_from_mpos(mpos);

    gc_sync_position();//This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
    plan_sync_position();
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

    //Alocate the memory to store the measurements in. This is used here because take slack will use the same memory as the calibration
    allocateCalibrationMemory();
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

// True if calibration is complete or take slack has been run
bool Maslow_::setupComplete() {
    return setupIsComplete;
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
    //These are used to set the browser side initial guess for the frame size
    log_data("$/" << M << "_tlX=" << tlX);
    log_data("$/" << M << "_tlY=" << tlY);
    log_data("$/" << M << "_trX=" << trX);
    log_data("$/" << M << "_trY=" << trY);
    log_data("$/" << M << "_brX=" << brX);

    String data = "CLBM:[";
    for (int i = 0; i < waypoint; i++) {
        data += "{bl:" + String(calibration_data[i][2]) + ",   br:" + String(calibration_data[i][3]) +
                ",   tr:" + String(calibration_data[i][1]) + ",   tl:" + String(calibration_data[i][0]) + "},";
    }
    data += "]";
    HeartBeatEnabled = false;
    log_data(data.c_str());
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

    // if we are stopping, stop any running job too
    allChannels.stopJob();
}

// Stop all the motors
void Maslow_::stopMotors() {
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
}

static void stopEverything() {
    sys.set_state(State::Alarm);
    protocol_disable_steppers();
}

// Panic function, stops all motors and sets state to alarm
void Maslow_::panic() {
    stop();
    stopEverything();
}

//Emergecy Stop
void Maslow_::eStop(String message) {
    log_error("Emergency stop! Stopping all motors");
    log_warn("The machine will not respond until turned off and back on again");
    stop();
    error = true;
    errorMessage = message;
    stopEverything();
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

/* Calculates and updates the center (X, Y) position based on the coordinates of the four corners
* (top-left, top-right, bottom-left, bottom-right) of a rectangular area. The center is determined
* by finding the intersection of the diagonals of the rectangle.
*/
void Maslow_::updateCenterXY() {
    double A = (trY - blY) / (trX - blX);
    double B = (brY - tlY) / (brX - tlX);
    centerX  = (brY - (B * brX) + (A * trX) - trY) / (A - B);
    centerY  = A * (centerX - trX) + trY;
}

// Prints out state
void Maslow_::getInfo() {
    log_data("MINFO: { \"homed\": " << (all_axis_homed() ? "true" : "false") << ","
          << "\"calibrationInProgress\": " << (calibrationInProgress ? "true" : "false") << ","
          << "\"tl\": " << axisTL.getPosition() << ","
          << "\"tr\": " << axisTR.getPosition() << ","
          << "\"br\": " << axisBR.getPosition() << ","
          << "\"bl\": " << axisBL.getPosition() << ","
          << "\"etl\": " << axisTL.getPositionError() << ","
          << "\"etr\": " << axisTR.getPositionError() << ","
          << "\"ebr\": " << axisBR.getPositionError() << ","
          << "\"ebl\": " << axisBL.getPositionError() << ","
          << "\"extended\": " << (allAxisExtended() ? "true" : "false")
          << "}");
}

void Maslow_::set_telemetry(bool enabled) {
    if (enabled) {
        // Start off the file with the length of each struct in it.
        FileStream* file = new FileStream(MASLOW_TELEM_FILE, "w", "sd");
        // write header
        TelemetryFileHeader header;
        header.structureSize = sizeof(TelemetryData);
        strcpy(header.version, VERSION_NUMBER);
        file->write(reinterpret_cast<uint8_t *>(&header), sizeof(TelemetryFileHeader));
        file->flush();
        delete file;
    } else {
        // TODO: not sure why this fails to find the file
        // std::string filePath = MASLOW_TELEM_FILE;
        // std::string newFilePath = filePath + "." + std::to_string(millis());
        // std::error_code ec;
        // log_info("renaming file: " + filePath + " to " + newFilePath);
        // stdfs::rename(filePath, newFilePath, ec);
        // if (ec) {
        //     log_error(std::string("Error renaming file: ") + ec.message());
        // }
    }
    telemetry_enabled = enabled;
    log_info("Telemetry: " << (enabled? "enabled" : "disabled"));
}

void Maslow_::log_telem_hdr_csv() {
    log_data(
       "millis," <<
       "tlCurrent," <<
       "trCurrent," <<
       "blCurrent," <<
       "brCurrent," <<
       "tlPower," <<
       "trPower," <<
       "blPower," <<
       "brPower," <<
       "tlSpeed," <<
       "trSpeed," <<
       "blSpeed," <<
       "brSpeed," <<
       "tlPos," <<
       "trPos," <<
       "blPos," <<
       "brPos," <<
       "extendedTL," <<
       "extendedTR," <<
       "extendedBL," <<
       "extendedBR," <<
       "extendingALL," <<
       "complyALL," <<
       "takeSlack," <<
       "safetyOn," <<
       "targetX," <<
       "targetY," <<
       "targetZ," <<
       "x," <<
       "y," <<
       "test," <<
       "pointCount," <<
       "waypoint," <<
       "calibrationGridSize," <<
       "holdTimer," <<
       "holding," <<
       "holdTime," <<
       "centerX," <<
       "centerY," <<
       "lastCallToPID," <<
       "lastMiss," <<
       "lastCallToUpdate," <<
       "extendCallTimer," <<
       "complyCallTimer");
}

void Maslow_::log_telem_pt_csv(TelemetryData data) {
    log_data(
       std::to_string(data.timestamp) + ","
       + std::to_string(data.tlCurrent) + ","
       + std::to_string(data.trCurrent)  + ","
       + std::to_string(data.blCurrent)  + ","
       + std::to_string(data.brCurrent)  + ","
       + std::to_string(data.tlPower) + ","
       + std::to_string(data.trPower) + ","
       + std::to_string(data.blPower) + ","
       + std::to_string(data.brPower) + ","
       + std::to_string(data.tlSpeed) + ","
       + std::to_string(data.trSpeed) + ","
       + std::to_string(data.blSpeed) + ","
       + std::to_string(data.brSpeed) + ","
       + std::to_string(data.tlPos) + ","
       + std::to_string(data.trPos) + ","
       + std::to_string(data.blPos) + ","
       + std::to_string(data.brPos) + ","
       + std::to_string(data.extendedTL) + ","
       + std::to_string(data.extendedTR) + ","
       + std::to_string(data.extendedBL) + ","
       + std::to_string(data.extendedBR) + ","
       + std::to_string(data.extendingALL) + ","
       + std::to_string(data.complyALL) + ","
       + std::to_string(data.takeSlack) + ","
       + std::to_string(data.safetyOn) + ","
       + std::to_string(data.targetX) + ","
       + std::to_string(data.targetY) + ","
       + std::to_string(data.targetZ) + ","
       + std::to_string(data.x) + ","
       + std::to_string(data.y) + ","
       + std::to_string(data.test) + ","
       + std::to_string(data.pointCount) + ","
       + std::to_string(data.waypoint) + ","
       + std::to_string(data.calibrationGridSize) + ","
       + std::to_string(data.holdTimer) + ","
       + std::to_string(data.holding) + ","
       + std::to_string(data.holdTime) + ","
       + std::to_string(data.centerX) + ","
       + std::to_string(data.centerY) + ","
       + std::to_string(data.lastCallToPID) + ","
       + std::to_string(data.lastMiss) + ","
       + std::to_string(data.lastCallToUpdate) + ","
       + std::to_string(data.extendCallTimer) + ","
       + std::to_string(data.complyCallTimer)
    )
}

TelemetryData Maslow_::get_telemetry_data() {
    TelemetryData data;

    // TODO: probably, we ought to use mutexes here?, but it is not implemented yet and
    // it may not matter much. the reads are generally of types that don't
    //if (xSemaphoreTake(telemetry_mutex, portMAX_DELAY)) {
    // Access shared variables here
    data.timestamp = millis();
    data.tlCurrent = axisTL.getMotorCurrent();
    data.trCurrent = axisTR.getMotorCurrent();
    data.blCurrent = axisBL.getMotorCurrent();
    data.brCurrent = axisBR.getMotorCurrent();

    data.tlPower = axisTL.getMotorPower();
    data.trPower = axisTR.getMotorPower();
    data.blPower = axisBL.getMotorPower();
    data.brPower = axisBR.getMotorPower();

    data.tlSpeed = axisTL.getBeltSpeed();
    data.trSpeed = axisTR.getBeltSpeed();
    data.blSpeed = axisBL.getBeltSpeed();
    data.brSpeed = axisBR.getBeltSpeed();

    data.tlPos = axisTL.getPosition();
    data.trPos = axisTR.getPosition();
    data.blPos = axisBL.getPosition();
    data.brPos = axisBR.getPosition();

    data.extendedTL          = extendedTL;
    data.extendedTR          = extendedTR;
    data.extendedBL          = extendedBL;
    data.extendedBR          = extendedBR;
    data.extendingALL        = extendingALL;
    data.complyALL           = complyALL;
    data.takeSlack           = takeSlack;
    data.safetyOn            = safetyOn;
    data.targetX             = targetX;
    data.targetY             = targetY;
    data.targetZ             = targetZ;
    data.x                   = x;
    data.y                   = y;
    data.test                = test;
    data.pointCount          = pointCount;
    data.waypoint            = waypoint;
    data.calibrationGridSize = calibrationGridSize;
    data.holdTimer           = holdTimer;
    data.holding             = holding;
    data.holdTime            = holdTime;
    data.centerX             = centerX;
    data.centerY             = centerY;
    data.lastCallToPID       = lastCallToPID;
    data.lastMiss            = lastMiss;
    data.lastCallToUpdate    = lastCallToUpdate;
    data.extendCallTimer     = extendCallTimer;
    data.complyCallTimer     = complyCallTimer;
    // xSemaphoreGive(telemetry_mutex);
    //}
    return data;
}
void Maslow_::write_telemetry_buffer(uint8_t* buffer, size_t length) {
    // Open the file in append mode
    FileStream* file = new FileStream(MASLOW_TELEM_FILE, "a", "sd");

    // Write the buffer to the file
    file->write(buffer, length);
    file->flush();

    // Close the file
    delete file;
}

void Maslow_::dump_telemetry(const char* file) {
    log_info("Dumping telemetry...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // open the file
    FileStream* f = new FileStream(MASLOW_TELEM_FILE, "r", "sd");
    if (f) {
        // read the size of each struct from the file
        TelemetryFileHeader header;
        f->read(reinterpret_cast<char*>(&header), sizeof(TelemetryFileHeader));
        log_info("Struct size " << header.structureSize);
        log_info("Dump version " << header.version);
        log_telem_hdr_csv();
        // TODO: check version and adapt?
        TelemetryData* data   = new TelemetryData();
        char*          buffer = new char[header.structureSize];
        while (f->available()) {
            f->read(&buffer[0], header.structureSize);
            // populate data from buffer
            memcpy(data, buffer, header.structureSize);
            // print the data
            log_telem_pt_csv(*data);
            // log_telem_pt(*data);
        }
        // delete the buffer
        delete[] buffer;
        // delete the data
        delete data;
    } else{
        log_info("File not found")
    }
    delete f;
}

// Called on utility core as a task to gather telemetry and write it to an SD log
void telemetry_loop(void* unused) {
    const int bufferSize = 5000;
    uint8_t buffer[bufferSize];
    int bufferIndex = 0;

    while (true) {
        if (Maslow.telemetry_enabled) {
            TelemetryData data = Maslow.get_telemetry_data();

            // Copy the telemetry data into the buffer
            memcpy(buffer + bufferIndex, reinterpret_cast<uint8_t*>(&data), sizeof(TelemetryData));
            // increment the index
            bufferIndex += sizeof(TelemetryData);

            // Check if the buffer is about to overflow
            if (bufferIndex >= bufferSize) {
                log_debug("!" << bufferIndex);
                Maslow.write_telemetry_buffer(buffer, bufferIndex);
                bufferIndex = 0;
            }
        } else {
            if (bufferIndex > 0) {
                Maslow.write_telemetry_buffer(buffer, bufferIndex);
                bufferIndex = 0;
            }
        }
        // Start with 2Hz-ish
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

Maslow_& Maslow_::getInstance() {
    static Maslow_ instance;
    return instance;
}

Maslow_& Maslow = Maslow.getInstance();
