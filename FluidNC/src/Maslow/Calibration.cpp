#include "Calibration.h"
#include "Maslow.h"

//------------------------------------------------------
//------------------------------------------------------ Homing and calibration functions
//------------------------------------------------------

// Constructor
Calibration::Calibration() {
    // Initialization code here
}
// -Maslow homing loop. This is used whenver any of the homing funcitons are active (belts extending or retracting)
void Calibration::home() {
    //run all the retract functions untill we hit the current limit
    if (retractingTL) {
        if (Maslow.axisTL.retract()) {
            retractingTL  = false;
            axis_homed[0] = true;
            extendedTL    = false;
        }
    }
    if (retractingTR) {
        if (Maslow.axisTR.retract()) {
            retractingTR  = false;
            axis_homed[1] = true;
            extendedTR    = false;
        }
    }
    if (retractingBL) {
        if (Maslow.axisBL.retract()) {
            retractingBL  = false;
            axis_homed[2] = true;
            extendedBL    = false;
        }
    }
    if (retractingBR) {
        if (Maslow.axisBR.retract()) {
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
                Maslow.axisBR.decompressBelt();
            if (millis() - extendCallTimer > 150)
                Maslow.axisBL.decompressBelt();
            if (millis() - extendCallTimer > 250)
                Maslow.axisTR.decompressBelt();
            if (millis() - extendCallTimer > 350)
                Maslow.axisTL.decompressBelt();
        }
        //then make all the belts comply until they are extended fully, or user terminates it
        else {
            if (!extendedTL)
                extendedTL = Maslow.axisTL.extend(extendDist);
            if (!extendedTR)
                extendedTR = Maslow.axisTR.extend(extendDist);
            if (!extendedBL)
                extendedBL = Maslow.axisBL.extend(extendDist);
            if (!extendedBR)
                extendedBR = Maslow.axisBR.extend(extendDist);
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
            Maslow.axisBR.decompressBelt();
            Maslow.axisBL.decompressBelt();
            Maslow.axisTR.decompressBelt();
            Maslow.axisTL.decompressBelt();
        } else if(millis() - complyCallTimer < 800){
            Maslow.axisTL.comply();
            Maslow.axisTR.comply();
            Maslow.axisBL.comply();
            Maslow.axisBR.comply();
        }
        else {
            Maslow.axisTL.stop();
            Maslow.axisTR.stop();
            Maslow.axisBL.stop();
            Maslow.axisBR.stop();
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
bool Calibration::takeSlackFunc() {
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

            float extension = Maslow._beltEndExtension + Maslow._armLength;
            
            //This should use it's own array, this is not calibration data
            float diffTL = calibration_data[2][0] - measurementToXYPlane(Maslow.computeTL(x, y, 0), Maslow.tlZ);
            float diffTR = calibration_data[2][1] - measurementToXYPlane(Maslow.computeTR(x, y, 0), Maslow.trZ);
            float diffBL = calibration_data[2][2] - measurementToXYPlane(Maslow.computeBL(x, y, 0), Maslow.blZ);
            float diffBR = calibration_data[2][3] - measurementToXYPlane(Maslow.computeBR(x, y, 0), Maslow.brZ);
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
void Calibration::calibration_loop() {
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
            Maslow.x                     = calibrationGrid[waypoint][0]; //Are these ever used anywhere?
            Maslow.y                     = calibrationGrid[waypoint][1];
            hold(250);
        }
    }
}

// Function to allocate memory for calibration arrays
void Calibration::allocateCalibrationMemory() {
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
void Calibration::deallocateCalibrationMemory() {
    delete[] calibrationGrid;
    calibrationGrid = nullptr;
    for (int i = 0; i < CALIBRATION_GRID_SIZE_MAX; ++i) {
        delete[] calibration_data[i];
        }
    delete[] calibration_data;
    calibration_data = nullptr;
}


//Takes a raw measurement, projects it into the XY plane, then adds the belt end extension and arm length to get the actual distance.
float Calibration::measurementToXYPlane(float measurement, float zHeight){

    float lengthInXY = sqrt(measurement * measurement - zHeight * zHeight);
    return lengthInXY + Maslow._beltEndExtension + Maslow._armLength; //Add the belt end extension and arm length to get the actual distance
}

/*
*Computes the current xy cordinates of the sled based on the lengths of the upper two belts
*/
bool Calibration::computeXYfromLengths(double TL, double TR, float &x, float &y) {
    double tlLength = TL;//measurementToXYPlane(TL, tlZ);
    double trLength = TR;//measurementToXYPlane(TR, trZ);

    //Find the intersection of the two circles centered at tlX, tlY and trX, trY with radii tlLength and trLength
    double d = sqrt((Maslow.tlX - Maslow.trX) * (Maslow.tlX - Maslow.trX) + (Maslow.tlY - Maslow.trY) * (Maslow.tlY - Maslow.trY));
    if (d > tlLength + trLength || d < abs(tlLength - trLength)) {
        log_info("Unable to determine machine position");
        return false;
    }
    
    double a = (tlLength * tlLength - trLength * trLength + d * d) / (2 * d);
    double h = sqrt(tlLength * tlLength - a * a);
    double x0 = Maslow.tlX + a * (Maslow.trX - Maslow.tlX) / d;
    double y0 = Maslow.tlY + a * (Maslow.trY - Maslow.tlY) / d;
    double rawX = x0 + h * (Maslow.trY - Maslow.tlY) / d;
    double rawY = y0 - h * (Maslow.trX - Maslow.tlX) / d;

    // Adjust to the centered coordinates
    x = rawX - Maslow.centerX;
    y = rawY - Maslow.centerY;

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
bool Calibration::take_measurement(float result[4], int dir, int run, int current) {

    //Shouldn't this be handled with the same code as below but with the direction set to UP?
    if (orientation == VERTICAL) {
        //first we pull two bottom belts tight one after another, if x<0 we pull left belt first, if x>0 we pull right belt first
        static bool BL_tight = false;
        static bool BR_tight = false;
        Maslow.axisTL.recomputePID();
        Maslow.axisTR.recomputePID();

        //On the left side of the sheet we want to pull the left belt tight first
        if (Maslow.x < 0) {
            if (!BL_tight) {
                if (Maslow.axisBL.pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
            if (!BR_tight) {
                if (Maslow.axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
        }

        //On the right side of the sheet we want to pull the right belt tight first
        else {
            if (!BR_tight) {
                if (Maslow.axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
            if (!BL_tight) {
                if (Maslow.axisBL.pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
        }

        //once both belts are pulled, take a measurement
        if (BR_tight && BL_tight) {
            //take measurement and record it to the calibration data array.
            result[0] = measurementToXYPlane(Maslow.axisTL.getPosition(), Maslow.tlZ);
            result[1] = measurementToXYPlane(Maslow.axisTR.getPosition(), Maslow.trZ);
            result[2] = measurementToXYPlane(Maslow.axisBL.getPosition(), Maslow.blZ);
            result[3] = measurementToXYPlane(Maslow.axisBR.getPosition(), Maslow.brZ);
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
                holdAxis1 = &Maslow.axisTL;
                holdAxis2 = &Maslow.axisTR;
                if (Maslow.x < 0) {
                    pullAxis1 = &Maslow.axisBL;
                    pullAxis2 = &Maslow.axisBR;
                } else {
                    pullAxis1 = &Maslow.axisBR;
                    pullAxis2 = &Maslow.axisBL;
                }
                break;
            case DOWN:
                holdAxis1 = &Maslow.axisBL;
                holdAxis2 = &Maslow.axisBR;
                if (Maslow.x < 0) {
                    pullAxis1 = &Maslow.axisTL;
                    pullAxis2 = &Maslow.axisTR;
                } else {
                    pullAxis1 = &Maslow.axisTR;
                    pullAxis2 = &Maslow.axisTL;
                }
                break;
            case LEFT:
                holdAxis1 = &Maslow.axisTL;
                holdAxis2 = &Maslow.axisBL;
                if (Maslow.y < 0) {
                    pullAxis1 = &Maslow.axisBR;
                    pullAxis2 = &Maslow.axisTR;
                } else {
                    pullAxis1 = &Maslow.axisTR;
                    pullAxis2 = &Maslow.axisBR;
                }
                break;
            case RIGHT:
                holdAxis1 = &Maslow.axisTR;
                holdAxis2 = &Maslow.axisBR;
                if (Maslow.y < 0) {
                    pullAxis1 = &Maslow.axisBL;
                    pullAxis2 = &Maslow.axisTL;
                } else {
                    pullAxis1 = &Maslow.axisTL;
                    pullAxis2 = &Maslow.axisBL;
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
            result[0] = measurementToXYPlane(Maslow.axisTL.getPosition(), Maslow.tlZ);
            result[1] = measurementToXYPlane(Maslow.axisTR.getPosition(), Maslow.trZ);
            result[2] = measurementToXYPlane(Maslow.axisBL.getPosition(), Maslow.blZ);
            result[3] = measurementToXYPlane(Maslow.axisBR.getPosition(), Maslow.brZ);
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
bool Calibration::take_measurement_avg_with_check(int waypoint, int dir) {
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
                        log_info(Maslow.axis_id_to_label(i).c_str() << " " << measurements[j][i]);
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
                float diffTL = measurements[0][0] - measurementToXYPlane(Maslow.computeTL(x, y, 0), Maslow.tlZ);
                float diffTR = measurements[0][1] - measurementToXYPlane(Maslow.computeTR(x, y, 0), Maslow.trZ);
                float diffBL = measurements[0][2] - measurementToXYPlane(Maslow.computeBL(x, y, 0), Maslow.blZ);
                float diffBR = measurements[0][3] - measurementToXYPlane(Maslow.computeBR(x, y, 0), Maslow.brZ);
                log_info("Center point off by: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);

                if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                    log_error("Center point off by over " << threshold << "mm");

                    if(!adjustFrameSizeToMatchFirstMeasurement()){
                        Maslow.eStop("Unable to find a valid frame size to match the first measurement");
                        calibrationInProgress = false;
                        waypoint              = 0;
                        criticalCounter       = 0;
                        freeMeasurements();
                        return false;
                    }
                }

                //Compute the current XY position from the top two belt measurements...needs to be redone because we've adjusted the frame size by here
                if(!computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y)){
                    Maslow.eStop("Unable to find machine position from measurements");
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
bool Calibration::move_with_slack(double fromX, double fromY, double toX, double toY) {
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
        if (Maslow.computeTL(fromX, fromY, 0) < Maslow.computeTL(toX, toY, 0)) {
            tlExtending = true;
        } else {
            tlExtending = false;
        }
        if (Maslow.computeTR(fromX, fromY, 0) < Maslow.computeTR(toX, toY, 0)) {
            trExtending = true;
        } else {
            trExtending = false;
        }
        if (Maslow.computeBL(fromX, fromY, 0) < Maslow.computeBL(toX, toY, 0)) {
            blExtending = true;
        } else {
            blExtending = false;
        }
        if (Maslow.computeBR(fromX, fromY, 0) < Maslow.computeBR(toX, toY, 0)) {
            brExtending = true;
        } else {
            brExtending = false;
        }

        //Set the target to the starting position
        Maslow.setTargets(fromX, fromY, 0);
    }

    //Decompress belts for 500ms...this happens by returning right away before running any of the rest of the code
    if (millis() - moveBeginTimer < 750 && withSlack) {
        if (orientation == VERTICAL) {
            Maslow.axisTL.recomputePID();
            Maslow.axisTR.recomputePID();
            Maslow.axisBL.decompressBelt();
            Maslow.axisBR.decompressBelt();
        } else {
            switch (direction) {
                case UP:
                    Maslow.axisBL.decompressBelt();
                    Maslow.axisBR.decompressBelt();
                    break;
                case DOWN:
                    Maslow.axisTL.decompressBelt();
                    Maslow.axisTR.decompressBelt();
                    break;
                case LEFT:
                    Maslow.axisTR.decompressBelt();
                    Maslow.axisBR.decompressBelt();
                    break;
                case RIGHT:
                    Maslow.axisTL.decompressBelt();
                    Maslow.axisBL.decompressBelt();
                    break;
            }
        }

        return false;
    }

    //Stop for 50ms
    //we need to stop motors after decompression was finished once
    else if (millis() - moveBeginTimer < 800) {
        Maslow.stopMotors();
        return false;
    }

    //Set the targets
    Maslow.setTargets(Maslow.getTargetX() + xStepSize, Maslow.getTargetY() + yStepSize, 0);

        //Check to see if we have reached our target position
    if (abs(Maslow.getTargetX() - toX) < 5 && abs(Maslow.getTargetY() - toY) < 5) {
        Maslow.stopMotors();
        Maslow.reset_all_axis();
        decompress = true;  //Reset for the next pass
        return true;
    }

    //In vertical orientation we want to move with the top two belts and always have the lower ones be slack
    if(orientation == VERTICAL){
        Maslow.axisTL.recomputePID();
        Maslow.axisTR.recomputePID();
        if(withSlack){
            Maslow.axisBL.comply();
            Maslow.axisBR.comply();
        }
        else{
            Maslow.axisBL.recomputePID();
            Maslow.axisBR.recomputePID();
        }
    }
    else{

        //For each belt we check to see if it should be slack
        if(withSlack && tlExtending){
            Maslow.axisTL.comply();
        }
        else{
            Maslow.axisTL.recomputePID();
        }

        if(withSlack && trExtending){
            Maslow.axisTR.comply();
        }
        else{
            Maslow.axisTR.recomputePID();
        }

        if(withSlack && blExtending){
            Maslow.axisBL.comply();
        }
        else{
            Maslow.axisBL.recomputePID();
        }

        if(withSlack && brExtending){
            Maslow.axisBR.comply();
        }
        else{
            Maslow.axisBR.recomputePID();
        }
    }

    return false;  //We have not yet reached our target position
}

// Direction from maslow current coordinates to the target coordinates
int Calibration::get_direction(double x, double y, double targetX, double targetY) {
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
bool Calibration::adjustFrameSizeToMatchFirstMeasurement() {

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
    Maslow.tlY = L;
    Maslow.trX = L;
    Maslow.trY = L;
    Maslow.brX = L;
    updateCenterXY();

    log_info("Frame size automaticlaly adjusted to " + std::to_string(Maslow.brX) + " by " + std::to_string(Maslow.trY));
    return true;
}

//The number of points high and wide  must be an odd number
bool Calibration::generate_calibration_grid() {

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
// void Calibration::printCalibrationGrid() {
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

// void Calibration::retractTL() {
//     //We allow other bells retracting to continue
//     retractingTL = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisTL.reset();
// }
// void Calibration::retractTR() {
//     retractingTR = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisTR.reset();
// }
// void Calibration::retractBL() {
//     retractingBL = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisBL.reset();
// }
// void Calibration::retractBR() {
//     retractingBR = true;
//     complyALL    = false;
//     extendingALL = false;
//     axisBR.reset();
// }
void Calibration::retractALL() {

    retractingTL = true;
    retractingTR = true;
    retractingBL = true;
    retractingBR = true;
    complyALL    = false;
    extendingALL = false;
    Maslow.axisTL.reset();
    Maslow.axisTR.reset();
    Maslow.axisBL.reset();
    Maslow.axisBR.reset();
    setupIsComplete = false;
}

void Calibration::extendALL() {

    if (!all_axis_homed()) {
        log_error("Please press Retract All before using Extend All");  //I keep getting everything set up for calibration and then this trips me up
        sys.set_state(State::Idle);
        return;
    }

    Maslow.stop();
    extendingALL = true;

    updateCenterXY();

    //extendCallTimer = millis();
}

/*
* This function is called once when calibration is started
*/
void Calibration::runCalibration() {

    //If we are at the first point we need to generate the grid before we can start
    if (waypoint == 0) {
        if(!generate_calibration_grid()){ //Fail out if the grid cannot be generated
            return;
        }
    }
    Maslow.stop();

    //Save the z-axis 'stop' position
    Maslow.targetZ = 0;
    Maslow.setZStop();

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
    if(computeXYfromLengths(measurementToXYPlane(Maslow.axisTL.getPosition(), Maslow.tlZ), measurementToXYPlane(Maslow.axisTR.getPosition(), Maslow.trZ), x, y)){
        
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
void Calibration::comply() {
    complyCallTimer = millis();
    retractingTL    = false;
    retractingTR    = false;
    retractingBL    = false;
    retractingBR    = false;
    extendingALL    = false;
    complyALL       = true;
    Maslow.axisTL.reset(); //This just resets the thresholds for pull tight
    Maslow.axisTR.reset();
    Maslow.axisBL.reset();
    Maslow.axisBR.reset();
}


//These are used to force one motor to rotate
void Calibration::TLI(){
    TLIOveride = true;
    overideTimer = millis();
}
void Calibration::TRI(){
    TRIOveride = true;
    overideTimer = millis();
}
void Calibration::BLI(){
    BLIOveride = true;
    overideTimer = millis();
}
void Calibration::BRI(){
    BRIOveride = true;
    overideTimer = millis();
}
void Calibration::TLO(){
    TLOOveride = true;
    overideTimer = millis();
}
void Calibration::TRO(){
    TROOveride = true;
    overideTimer = millis();
}
void Calibration::BLO(){
    BLOOveride = true;
    overideTimer = millis();
}
void Calibration::BRO(){
    BROOveride = true;
    overideTimer = millis();
}

/*
* This function is used to manuall force the motors to move for a fraction of a second to clear jams
*/
void Calibration::handleMotorOverides(){
    if(TLIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisTL.fullIn();
        }else{
            TLIOveride = false;
            Maslow.axisTL.stop();
        }
    }
    if(BRIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisBR.fullIn();
        }else{
            BRIOveride = false;
            Maslow.axisBR.stop();
        }
    }
    if(TRIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisTR.fullIn();
        }else{
            TRIOveride = false;
            Maslow.axisTR.stop();
        }
    }
    if(BLIOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisBL.fullIn();
        }else{
            BLIOveride = false;
            Maslow.axisBL.stop();
        }
    }
    if(TLOOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisTL.fullOut();
        }else{
            TLOOveride = false;
            Maslow.axisTL.stop();
        }
    }
    if(BROOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisBR.fullOut();
        }else{
            BROOveride = false;
            Maslow.axisBR.stop();
        }
    }
    if(TROOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisTR.fullOut();
        }else{
            TROOveride = false;
            Maslow.axisTR.stop();
        }
    }
    if(BLOOveride){
        log_info(int(millis() - overideTimer));
        if(millis() - overideTimer < 200){
            Maslow.axisBL.fullOut();
        }else{
            BLOOveride = false;
            Maslow.axisBL.stop();
        }
    }
}

bool Calibration::checkOverides(){
    if(TLIOveride || TRIOveride || BLIOveride || BRIOveride || TLOOveride || TROOveride || BLOOveride || BROOveride){
        return true;
    }
    return false;
}

void Calibration::setSafety(bool state) {
    safetyOn = state;
}

void Calibration::take_slack() {
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
    Maslow.axisTL.reset();
    Maslow.axisTR.reset();
    Maslow.axisBL.reset();
    Maslow.axisBR.reset();

    Maslow.x         = 0;
    Maslow.y         = 0;
    takeSlack = true;

    //Alocate the memory to store the measurements in. This is used here because take slack will use the same memory as the calibration
    allocateCalibrationMemory();
}



// True if all axis were zeroed
bool Calibration::all_axis_homed() {
    return axis_homed[0] && axis_homed[1] && axis_homed[2] && axis_homed[3];
}

// True if all axis were extended
bool Calibration::allAxisExtended() {
    return extendedTL && extendedTR && extendedBL && extendedBR;
}

// True if calibration is complete or take slack has been run
bool Calibration::setupComplete() {
    return setupIsComplete;
}



//Checks to see if the calibration data needs to be sent again
void Calibration::checkCalibrationData() {
    if (calibrationDataWaiting > 0) {
        if (millis() - calibrationDataWaiting > 30007) {
            log_error("Calibration data not acknowledged by computer, resending");
            print_calibration_data();
            calibrationDataWaiting = millis();
        }
    }
}

// function for outputting calibration data in the log line by line like this: {bl:2376.69,   br:923.40,   tr:1733.87,   tl:2801.87},
void Calibration::print_calibration_data() {
    //These are used to set the browser side initial guess for the frame size
    log_data("$/" << M << "_tlX=" << Maslow.tlX);
    log_data("$/" << M << "_tlY=" << Maslow.tlY);
    log_data("$/" << M << "_trX=" << Maslow.trX);
    log_data("$/" << M << "_trY=" << Maslow.trY);
    log_data("$/" << M << "_brX=" << Maslow.brX);

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
void Calibration::calibrationDataRecieved(){
    // log_info("Calibration data acknowledged received by computer");
    calibrationDataWaiting = -1;
}

/* Calculates and updates the center (X, Y) position based on the coordinates of the four corners
* (top-left, top-right, bottom-left, bottom-right) of a rectangular area. The center is determined
* by finding the intersection of the diagonals of the rectangle.
*/
void Calibration::updateCenterXY() {
    double A = (Maslow.trY - Maslow.blY) / (Maslow.trX - Maslow.blX);
    double B = (Maslow.brY - Maslow.tlY) / (Maslow.brX - Maslow.tlX);
    Maslow.centerX  = (Maslow.brY - (B * Maslow.brX) + (A * Maslow.trX) - Maslow.trY) / (A - B);
   Maslow.centerY  = A * (Maslow.centerX - Maslow.trX) + Maslow.trY;
}

//non-blocking delay, just pauses everything for specified time
void Calibration::hold(unsigned long time) {
    holdTime  = time;
    holding   = true;
    holdTimer = millis();
}