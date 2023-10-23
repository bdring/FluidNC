#include "Maslow.h"
#include "../Report.h"

// Maslow specific defines
#define TLEncoderLine 0
#define TREncoderLine 2
#define BLEncoderLine 1
#define BREncoderLine 3

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

#define MEASUREMENTSPEED 1.0 //The max speed at which we move the motors when taking measurements

int lowerBeltsExtra = 4;
int callsSinceDelay = 0;

void Maslow_::begin(void (*sys_rt)()) {
  initialized = 1;

  //Serial.begin(115200);
  Wire.begin(5,4, 200000);
  I2CMux.begin(TCAADDR, Wire);

  axisTL.begin(tlIn1Pin, tlIn2Pin, tlADCPin, TLEncoderLine, tlIn1Channel, tlIn2Channel);
  axisTR.begin(trIn1Pin, trIn2Pin, trADCPin, TREncoderLine, trIn1Channel, trIn2Channel);
  axisBL.begin(blIn1Pin, blIn2Pin, blADCPin, BLEncoderLine, blIn1Channel, blIn2Channel);
  axisBR.begin(brIn1Pin, brIn2Pin, brADCPin, BREncoderLine, brIn1Channel, brIn2Channel);

  axisBL.zero();
  axisBR.zero();
  axisTR.zero();
  axisTL.zero();
  
  axisBLHomed = false;
  axisBRHomed = false;
  axisTRHomed = false;
  axisTLHomed = false;

//   tlX =-3.127880538461895;
//   tlY = 2063.1006937512166;
//   tlZ = 116 + 38;
//   trX = 2944.4878198392585; 
//   trY = 2069.656171241167;
//   trZ = 69 + 38;
//   blX = 0;
//   blY = 0;
//   blZ = 47 + 38;
//   brX = 2959.4124827780993;
//   brY = 0;
//   brZ = 89 + 38;

  tlX = 5.5;
  tlY = 2150;
  tlZ = 0;
  trX = 3135; 
  trY = 2150;
  trZ = 0;
  blX = 0;
  blY = 0;
  blZ = 0;
  brX = 3095;
  brY = 0;
  brZ = 0;


  tlTension = 0;
  trTension = 0;
  
  //Recompute the center XY
  updateCenterXY();
  
  _beltEndExtension = 30; //Based on the CAD model these should add to 153.4
  _armLength = 123.4;

  extendingOrRetracting = false;
  calibrationInProgress = false;

  _sys_rt = sys_rt;

  pinMode(coolingFanPin, OUTPUT);

  pinMode(SERVOFAULT, INPUT);

  currentThreshold = 1500;
}


void Maslow_::home(int axis) {
    // log_info(axis);
    // switch(axis) {
    //     case 0: //Bottom left
    //         extendingOrRetracting = true;
    //         axisBLHomed = axisBL.retract(computeBL(0, 300, 0));
    //         extendingOrRetracting = false;
    //         break;
    //     case 1: //Top Left
    //         if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
    //             lowerBeltsExtra = lowerBeltsExtra - 1;
    //             log_info("Extra: " << lowerBeltsExtra);
    //         }
    //         else{
    //             extendingOrRetracting = true;
    //             axisTLHomed = axisTL.retract(computeTL(0, 0, 0));
    //             extendingOrRetracting = false;
    //         }
    //         break;
    //     case 2: //Top right
    //         if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
    //             lowerBeltsExtra = lowerBeltsExtra + 1;
    //             log_info("Extra: " << lowerBeltsExtra);
    //         }
    //         else{
    //             extendingOrRetracting = true;
    //             axisTRHomed = axisTR.retract(computeTR(0, 0, 0));
    //             extendingOrRetracting = false;
    //         }
    //         break;
    //     case 4: //Bottom right
    //         if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
    //             runCalibration();
    //         }
    //         else {
    //             extendingOrRetracting = true;
    //             axisBRHomed = axisBR.retract(computeBR(0, 300, 0));
    //             extendingOrRetracting = false;
    //         }
    //         break;
    //     default:
    //         log_info("Unrecognized axis");
    //         log_info(axis);
    //         break;
    // }

    // if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
    //     log_info("All axis ready.");
    // }
}
// Maslow main loop
void Maslow_::update(){
    //Make sure we're running maslow config file
    if(!Maslow.using_default_config){

        Maslow.updateEncoderPositions(); //We always update encoder positions in any state

        //Maslow State Machine
        if( sys.state() == State::Jog || sys.state() == State::Cycle  ){

            Maslow.setTargets(steps_to_mpos(get_axis_motor_steps(0),0), steps_to_mpos(get_axis_motor_steps(1),1), steps_to_mpos(get_axis_motor_steps(2),2));
            Maslow.recomputePID();
        }

        else if(sys.state() == State::Homing){

            //run all the retract functions untill we hit the current limit
            if(retractingBR){
                if( axisBR.retract() ) retractingBR = false;
                if(random(40) == 10){
                    log_info(axisBR.getCurrent());
                }
            }
            if(retractingTL){
                if( axisTL.retract() ) retractingTL = false;
            }
            if(retractingTR){
                if( axisTR.retract() ) retractingTR = false;
            }
            if(retractingBL){
                if( axisBL.retract() ) retractingBL = false;
            }

            //Extending routines
            if (extendingALL) {
                //decompress belts for the first half second
                if (millis() - extendCallTimer < 500) {
                    if( millis() - extendCallTimer >0) axisBR.decompressBelt();
                    if (millis() - extendCallTimer > 100) axisBL.decompressBelt();
                    if (millis() - extendCallTimer > 150) axisTR.decompressBelt();
                    if (millis() - extendCallTimer > 200) axisTL.decompressBelt();
                } 
                //then make all the belts comply until they are extended fully, or user terminates it
                else {
                    if(axisTL.extend(computeTL(0, 0, 0)) && axisTR.extend(computeTR(0, 0, 0)) && axisBL.extend(computeBL(0, 300, 0)) && axisBR.extend(computeBR(0, 300, 0))){
                        extendingALL = false;
                        log_info("All belts extended");
                    }
                }
            }

            if(complyALL){
                //decompress belts for the first half second
                if (millis() - complyCallTimer < 500) {
                    if( millis() - complyCallTimer >0) axisBR.decompressBelt();
                    if (millis() - complyCallTimer > 100) axisBL.decompressBelt();
                    if (millis() - complyCallTimer > 150) axisTR.decompressBelt();
                    if (millis() - complyCallTimer > 200) axisTL.decompressBelt();
                } else {
                    axisTL.comply(500);  //call to recomputePID() inside here
                    axisTR.comply(500);
                    axisBL.comply(500);
                    axisBR.comply(500);
                }
            }
            //if we are done with all the homing moves, switch system state back to alarm ( or Idle? )
            if(!retractingTL && !retractingBL && !retractingBR && !retractingTR && !extendingALL && !complyALL ){
                sys.set_state(State::Alarm);
            }
            
        }
        else Maslow.stopMotors();
        // static int n = 0;
        // long tp = millis() - lastCallToUpdate;
        // String s = String(tp);
        // if(n++ % 250 == 0) log_info(s.c_str());

        //if the update function is not being called enough, stop motors to prevent damage
        // if(millis() - lastCallToUpdate > 500){
        //     Maslow.stopMotors();
        // }

    }
    lastCallToUpdate = millis();
}

//non-blocking homing functions
void Maslow_::retractTL(){
    retractingTL = true;
    axisTL.reset();
    log_info("Retracting Top Left");
}
void Maslow_::retractTR(){
    retractingTR = true;
    axisTR.reset();
    log_info("Retracting Top Right");
}
void Maslow_::retractBL(){
    retractingBL = true;
    axisBL.reset();
    log_info("Retracting Bottom Left");
}
void Maslow_::retractBR(){
    retractingBR = true;
    axisBR.reset();
    log_info("Retracting Bottom Right");
}
void Maslow_::retractALL(){
    retractingTL = true;
    retractingTR = true;
    retractingBL = true;
    retractingBR = true;
    complyALL = false;
    extendingALL = false;
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
    log_info("Retracting All");
}
void Maslow_::extendALL(){
    extendCallTimer = millis();
    retractingTL = false;
    retractingTR = false;
    retractingBL = false;
    retractingBR = false;
    complyALL = false;
    extendingALL = true;
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
    log_info("Extending All");
}
void Maslow_::comply(){
    complyCallTimer = millis();
    retractingTL = false;
    retractingTR = false;
    retractingBL = false;
    retractingBR = false;
    extendingALL = false;
    complyALL = true;
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
    log_info("Complying All");
}
//updating encoder positions for all 4 arms
bool Maslow_::updateEncoderPositions(){
    bool success = true;
    if(!readingFromSD){
        if(!axisBL.updateEncoderPosition()) success = false;
        if(!axisBR.updateEncoderPosition()) success = false;
        if(!axisTR.updateEncoderPosition()) success = false;
        if(!axisTL.updateEncoderPosition()) success = false;
    }
    return success;
}
//Called from protocol.cpp
void Maslow_::recomputePID(){

    if(!initialized){ //If we haven't initialized we don't want to try to compute things because the PID controllers cause the processor to crash
        return;
    }
    if(readingFromSD){
        return;
    }

    int timeSinceLastCall = millis() - lastCallToPID;
    
    if(timeSinceLastCall > 20){
        int elapsedTimeLastMiss = millis() - lastMiss;
        //log_info( "PID not being called often enough. Ms since last call: " << timeSinceLastCall << " # since last miss: " << callsSinceDelay << " Ms since last miss: " << elapsedTimeLastMiss);
        callsSinceDelay = 0;
        lastMiss = millis();
    }
    else{
        callsSinceDelay++;
    }

    lastCallToPID = millis();

    //If the belt is extending or retracting from the zero point we don't do anything here
    if(extendingOrRetracting){
        return;
    }

    //Stop the motors if we are idle or alarm. Unless doing calibration. Calibration can happen during idle or alarm
    if((sys.state() == State::Idle || sys.state() == State::Alarm) && !calibrationInProgress){
        axisBL.stop();
        axisBR.stop();
        axisTR.stop();
        axisTL.stop();
        digitalWrite(coolingFanPin, LOW); //Turn off the cooling fan
    }
    else{  //Normal operation...drive the motors to the target positions
        if(random(50) == 0){
            //log_info("Recomputing PID called");
        }
        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();
        digitalWrite(coolingFanPin, HIGH); //Turn on the cooling fan
    }

    if(digitalRead(SERVOFAULT) == 1){
        log_info("Servo fault!");
    }
}
//Stop all the motors
void Maslow_::stopMotors(){
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
}
//Computes the tensions in the upper two belts
void Maslow_::computeTensions(float x, float y){
    //This should be a lot smarter and compute the vector tensions to see if the lower belts are contributing positively

    //These internal version move the center to (0,0)

    float tlXi = tlX - trX / 2.0;
    float tlYi = tlY / 2.0;
    float trXi = trX / 2.0;

    float A = atan((y-tlYi)/(trXi - x));
    float B = atan((y-tlYi)/(x-tlXi));

    trTension = 1 / (cos(A) * sin(B) / cos(B) + sin(A));
    tlTension = 1 / (cos(B) * sin(A) / cos(A) + sin(B));

    // if(random(400) == 10){
    //     grbl_sendf( "TL Tension: %f TR Tension: %f\n", tlTension, trTension);
    // }
}

//Bottom left belt
float Maslow_::computeBL(float x, float y, float z){
    //Move from lower left corner coordinates to centered coordinates
    x = x + centerX;
    y = y + centerY;
    float a = blX - x;
    float b = blY - y;
    float c = 0.0 - (z + blZ);

    float length = sqrt(a*a+b*b+c*c) - (_beltEndExtension+_armLength);

    //Add some extra slack if this belt isn't needed because the upper belt is already very taught
    //Max tension is around -1.81 at the very top and -.94 at the bottom
    float extraSlack = min(max(-34.48*trTension - 32.41, 0.0), 8.0); //limit of 0-2mm of extension

    // if(random(4000) == 10){
    //     grbl_sendf( "BL Slack By: %f\n", extraSlack);
    // }

    return length + lowerBeltsExtra;
}

//Bottom right belt
float Maslow_::computeBR(float x, float y, float z){
    //Move from lower left corner coordinates to centered coordinates
    x = x + centerX;
    y = y + centerY;
    float a = brX - x;
    float b = brY - y;
    float c = 0.0 - (z + brZ);

    float length = sqrt(a*a+b*b+c*c) - (_beltEndExtension+_armLength);

    float extraSlack = min(max(-34.48*tlTension - 32.41, 0.0), 8.0); //limit of 0-2mm of extension

    // if(random(4000) == 10){
    //     grbl_sendf( "BR Slack By: %f\n", extraSlack);
    // }

    return length + lowerBeltsExtra;
}

//Top right belt
float Maslow_::computeTR(float x, float y, float z){
    //Move from lower left corner coordinates to centered coordinates
    x = x + centerX;
    y = y + centerY;
    float a = trX - x;
    float b = trY - y;
    float c = 0.0 - (z + trZ);
    return sqrt(a*a+b*b+c*c) - (_beltEndExtension+_armLength);
}

//Top left belt
float Maslow_::computeTL(float x, float y, float z){
    //Move from lower left corner coordinates to centered coordinates
    x = x + centerX;
    y = y + centerY;
    float a = tlX - x;
    float b = tlY - y;
    float c = 0.0 - (z + tlZ);
    return sqrt(a*a+b*b+c*c) - (_beltEndExtension+_armLength);
}

void Maslow_::setTargets(float xTarget, float yTarget, float zTarget){

    //Scaling to correct size
    xTarget = xTarget;
    yTarget = yTarget;
    
    if(!calibrationInProgress){

        computeTensions(xTarget, yTarget);

        axisBL.setTarget(computeBL(xTarget, yTarget, zTarget));
        axisBR.setTarget(computeBR(xTarget, yTarget, zTarget));
        axisTR.setTarget(computeTR(xTarget, yTarget, zTarget));
        axisTL.setTarget(computeTL(xTarget, yTarget, zTarget));
    }
}

void Maslow_::printMeasurementSet(float allLengths[][4]){

    log_info("{bl:" << allLengths[0][0] << ",   br:" << allLengths[0][1] << ",   tr:" << allLengths[0][2] << ",   tl:" << allLengths[0][3] << "}");
    log_info("{bl:" << allLengths[1][0] << ",   br:" << allLengths[1][1] << ",   tr:" << allLengths[1][2] << ",   tl:" << allLengths[1][3] << "}");
    log_info("{bl:" << allLengths[2][0] << ",   br:" << allLengths[2][1] << ",   tr:" << allLengths[2][2] << ",   tl:" << allLengths[2][3] << "}");
    log_info("{bl:" << allLengths[3][0] << ",   br:" << allLengths[3][1] << ",   tr:" << allLengths[3][2] << ",   tl:" << allLengths[3][3] << "}");
    log_info("{bl:" << allLengths[4][0] << ",   br:" << allLengths[4][1] << ",   tr:" << allLengths[4][2] << ",   tl:" << allLengths[4][3] << "}");
    log_info("{bl:" << allLengths[5][0] << ",   br:" << allLengths[5][1] << ",   tr:" << allLengths[5][2] << ",   tl:" << allLengths[5][3] << "}");
    log_info("{bl:" << allLengths[6][0] << ",   br:" << allLengths[6][1] << ",   tr:" << allLengths[6][2] << ",   tl:" << allLengths[6][3] << "}");
    log_info("{bl:" << allLengths[7][0] << ",   br:" << allLengths[7][1] << ",   tr:" << allLengths[7][2] << ",   tl:" << allLengths[7][3] << "}");
    log_info("{bl:" << allLengths[8][0] << ",   br:" << allLengths[8][1] << ",   tr:" << allLengths[8][2] << ",   tl:" << allLengths[8][3] << "}");
    log_info("{bl:" << allLengths[9][0] << ",   br:" << allLengths[9][1] << ",   tr:" << allLengths[9][2] << ",   tl:" << allLengths[9][3] << "}");

    (*_sys_rt)();
        
    // Delay without blocking
    unsigned long time = millis();
    unsigned long elapsedTime = millis()-time;
    while(elapsedTime < 250){
        elapsedTime = millis()-time;
        (*_sys_rt)();
    }
}

//Takes one column of 10 measurements
void Maslow_::takeColumnOfMeasurements(float x, float measurments[][4]){
//IS NOT WORKING, DONT UNCOMMENT
    float measurement1[4] = {0};
    float measurement2[4] = {0};
    float measurement3[4] = {0};
    float measurement4[4] = {0};
    float measurement5[4] = {0};
    float measurement6[4] = {0};
    float measurement7[4] = {0};
    float measurement8[4] = {0};
    float measurement9[4] = {0};
    float measurement10[4] = {0};

    //Move to where we need to begin
    moveWithSlack(x, 550, true, true);

    //First measurmement
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); } //If we are on the left side of the sheet tension the left belt first
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); } //If we are on the right side of the sheet tension the right belt first
    
    takeMeasurementAvgWithCheck(measurments[0]);
    
    //Second measurmement
    moveWithSlack(x, 425, false, false);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[1]);

    //Third measurmement
    moveWithSlack(x, 300, false, false);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[2]);

    //Fourth measurement
    moveWithSlack(x, 200, false, true);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[3]);
    
    //Fifth measurement
    moveWithSlack(x, 100, false, true);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[4]);

    //Sixth measurmement
    moveWithSlack(x, 0, false, false);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[5]);

    //Sevent measurmement
    moveWithSlack(x, -100, false, false);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[6]);

    //Eigth measurement
    moveWithSlack(x, -200, false, true);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[7]);
    
    //Ninth measurement
    moveWithSlack(x, -300, false, true);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[8]);

    //Tenth measurement
    moveWithSlack(x, -400, false, true);
    if(x < 0) { retractBL_CAL(); retractBL_CAL(); }
    if(x > 0) { retractBR_CAL(); retractBR_CAL(); }
    takeMeasurementAvgWithCheck(measurments[9]);
    
}

//Runs the calibration sequence to determine the machine's dimensions
void Maslow_::runCalibration(){
    
    log_info("\n\nBeginning calibration\n\n");
    
    calibrationInProgress = true;
    
    //Undoes any calls by the system to move these to (0,0)
    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());
    axisTR.setTarget(axisTR.getPosition());
    axisTL.setTarget(axisTL.getPosition());
    
    //Take the measurements
    float column1[10][4] = {0};
    float column2[10][4] = {0};
    float column3[10][4] = {0};
    float column4[10][4] = {0};
    float column5[10][4] = {0};
    float column6[10][4] = {0};
    float column7[10][4] = {0};
    float column8[10][4] = {0};
    float column9[10][4] = {0};
    float column10[10][4] = {0};
    float column11[10][4] = {0};

    takeColumnOfMeasurements(-800, column1);
    takeColumnOfMeasurements(-640, column2);
    takeColumnOfMeasurements(-480, column3);
    takeColumnOfMeasurements(-320, column4);
    takeColumnOfMeasurements(-160, column5);
    takeColumnOfMeasurements(0, column6);
    takeColumnOfMeasurements(160, column7);
    takeColumnOfMeasurements(320, column8);
    takeColumnOfMeasurements(480, column9);
    takeColumnOfMeasurements(640, column10);
    takeColumnOfMeasurements(800, column11);

    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();

    printMeasurementSet(column1);
    printMeasurementSet(column2);
    printMeasurementSet(column3);
    printMeasurementSet(column4);
    printMeasurementSet(column5);
    printMeasurementSet(column6);
    printMeasurementSet(column7);
    printMeasurementSet(column8);
    printMeasurementSet(column9);
    printMeasurementSet(column10);
    printMeasurementSet(column11);
    
    //----------------------------------------------------------Do the computation
    
    // printMeasurements(lengths1);
    // printMeasurements(lengths2);
    // printMeasurements(lengths3);
    // printMeasurements(lengths4);
    // printMeasurements(lengths5);
    // printMeasurements(lengths6);
    // printMeasurements(lengths7);
    // printMeasurements(lengths8);
    // printMeasurements(lengths9);
    
    // double measurements[][4] = {
    //     //TL              TR           BL           BR
    //     {lengths1[3], lengths1[2], lengths1[0], lengths1[1]},
    //     {lengths2[3], lengths2[2], lengths2[0], lengths2[1]},
    //     {lengths3[3], lengths3[2], lengths3[0], lengths3[1]},
    //     {lengths4[3], lengths4[2], lengths4[0], lengths4[1]},
    //     {lengths5[3], lengths5[2], lengths5[0], lengths5[1]},
    //     {lengths6[3], lengths6[2], lengths6[0], lengths6[1]},
    //     {lengths7[3], lengths7[2], lengths7[0], lengths7[1]},
    //     {lengths8[3], lengths8[2], lengths8[0], lengths8[1]},
    //     {lengths9[3], lengths9[2], lengths9[0], lengths9[1]},
    // };
    // double results[6] = {0,0,0,0,0,0};
    // computeCalibration(measurements, results, printToWeb, tlX, tlY, trX, trY, brX, tlZ, trZ, blZ, brZ);
    
    // log_info( "After computing calibration " + String(results[5]));
    
    // if(results[5] < 2){
    //     log_info( "Calibration successful with precision: " + String(results[5]));
    //     tlX = results[0];
    //     tlY = results[1];
    //     trX = results[2];
    //     trY = results[3];
    //     blX = 0;
    //     blY = 0;
    //     brX = results[4];
    //     brY = 0;
    //     updateCenterXY();
    //     log_info( "tlx: " + String(tlX) + " tly: " + String(tlY) + 
    //         "\ntrX: " + String(trX) + " trY: " + String(trY) + 
    //         "\nblX: " + String(blX) + " blY: " + String(blY) + 
    //         "\nbrx: " + String(brX) + " brY: " + String(brY));
    // }
    // else{
    //     log_info( "Calibration failed: " + String(results[5]));
    // }
    
    // //---------------------------------------------------------Finish
    
    
    //Move back to center after the results are applied
    moveWithSlack(0, 0, true, true);
    
    // //For safety we should pull tight here and verify that the results are basically what we expect before handing things over to the controller.
    // float allLengths[5][4];
    // takeMeasurementAvg(lengths1, allLengths);
    // takeMeasurementAvg(lengths1, allLengths);
    
    // double blError = (lengths1[0]-(_beltEndExtension+_armLength))-computeBL(0,0,0);
    // double brError = (lengths1[1]-(_beltEndExtension+_armLength))-computeBR(0,0,0);
    
    // log_info( "Lower belt length mismatch: " + String(blError) + ", " +String(brError));
    
    calibrationInProgress = false;
    log_info( "Calibration finished");
    
}


float Maslow_::printMeasurementMetrics(double avg, double m1, double m2, double m3, double m4, double m5){
    
    //grbl_sendf( "Avg: %f m1: %f, m2: %f, m3: %f, m4: %f, m5: %f\n", avg, m1, m2, m3, m4, m5);
    
    double m1Variation = myAbs(avg - m1);
    double m2Variation = myAbs(avg - m2);
    double m3Variation = myAbs(avg - m3);
    double m4Variation = myAbs(avg - m4);
    double m5Variation = myAbs(avg - m5);

    float maxDeviation = std::max({m1Variation, m2Variation,m3Variation, m4Variation, m5Variation});
    
    //log_info( "Max deviation: " + String(maxDeviation));

    //double avgDeviation = (m1Variation + m2Variation + m3Variation + m4Variation + m5Variation)/5.0;
    
    //grbl_sendf( "Avg deviation: %f\n", avgDeviation);

    return maxDeviation;
}

//Checks to make sure the deviation within the measurement avg looks good before moving on
void Maslow_::takeMeasurementAvgWithCheck(float allLengths[4]){
    float threshold = 0.5;
    while(true){
        float repeatability = takeMeasurementAvg(allLengths);
        if(repeatability < threshold){
            log_info( "Using measurement with precision:");
            log_info(repeatability);
            break;
        }
        log_info( "Repeating measurement");
    }
}

// Takes 5 measurements and returns how consistent they are
float Maslow_::takeMeasurementAvg(float allLengths[4]) {

    // Where our five measurements will be stored
    float lengths1[4];
    float lengths2[4];
    float lengths3[4];
    float lengths4[4];
    float lengths5[4];

    //Where the average lengths will be stored
    float avgLengths[4];

    takeMeasurement(lengths1);
    takeMeasurement(lengths1);  // Repeat the first measurement to discard the one before everything was pulled taught
    takeMeasurement(lengths2);
    takeMeasurement(lengths3);
    takeMeasurement(lengths4);
    takeMeasurement(lengths5);

    allLengths[0] = lengths5[0];
    allLengths[1] = lengths5[1];
    allLengths[2] = lengths5[2];
    allLengths[3] = lengths5[3];

    avgLengths[0] = (lengths1[0] + lengths2[0] + lengths3[0] + lengths4[0] + lengths5[0]) / 5.0;
    avgLengths[1] = (lengths1[1] + lengths2[1] + lengths3[1] + lengths4[1] + lengths5[1]) / 5.0;
    avgLengths[2] = (lengths1[2] + lengths2[2] + lengths3[2] + lengths4[2] + lengths5[2]) / 5.0;
    avgLengths[3] = (lengths1[3] + lengths2[3] + lengths3[3] + lengths4[3] + lengths5[3]) / 5.0;

    float m1 = printMeasurementMetrics(avgLengths[0], lengths1[0], lengths2[0], lengths3[0], lengths4[0], lengths5[0]);
    float m2 = printMeasurementMetrics(avgLengths[1], lengths1[1], lengths2[1], lengths3[1], lengths4[1], lengths5[1]);
    float m3 = printMeasurementMetrics(avgLengths[2], lengths1[2], lengths2[2], lengths3[2], lengths4[2], lengths5[2]);
    float m4 = printMeasurementMetrics(avgLengths[3], lengths1[3], lengths2[3], lengths3[3], lengths4[3], lengths5[3]);

    float maxDeviation = std::max({m1, m2, m3, m4});

    log_info("Max Deviation: " << maxDeviation);

    return maxDeviation;
}

//Retract the lower belts until they pull tight and take a measurement
void Maslow_::takeMeasurement(float lengths[]){
    log_info( "Taking a measurement.");

    extendingOrRetracting = true;

    axisBL.stop();
    axisBR.stop();
    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());

    bool axisBLDone = false;
    bool axisBRDone = false;

    float BLDist = .01;
    float BRDist = .01;
    
    while(!axisBLDone || !axisBRDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        
        if(axisBL.getCurrent() > currentThreshold || axisBLDone){  //Check if the current threshold is hit
            axisBLDone = true;
        }
        else{                                                       //If not
            axisBL.setTarget(axisBL.getPosition() - BLDist);                  //Pull in a little more
            BLDist = min(MEASUREMENTSPEED, BLDist + .001);                                     //Slowly ramp up the speed
        }
        
        if(axisBR.getCurrent() > currentThreshold || axisBRDone){
            axisBRDone = true;
        }
        else{
            axisBR.setTarget(axisBR.getPosition() - BRDist);
            BRDist = min(MEASUREMENTSPEED, BRDist + .001);
        }

        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisBL.updateEncoderPosition();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
            (*_sys_rt)();
        }
    }
    
    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());
    //axisTR.setTarget(axisTR.getPosition());
    //axisTL.setTarget(axisTL.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
    
    lengths[0] = axisBL.getPosition()+_beltEndExtension+_armLength;
    lengths[1] = axisBR.getPosition()+_beltEndExtension+_armLength;
    lengths[2] = axisTR.getPosition()+_beltEndExtension+_armLength;
    lengths[3] = axisTL.getPosition()+_beltEndExtension+_armLength;
    
    log_info("Measurement finished");
    log_info("{bl:" << lengths[0] << ", br:" << lengths[1] << ",   tr:" << lengths[2] << ",  tl:" <<lengths[3] << "}");
    //log_info( "Measured:\n%f, %f \n%f %f \n",lengths[3], lengths[2], lengths[0], lengths[1]);
    
    extendingOrRetracting = false;

    return;
}

//Retract the lower right belt
void Maslow_::retractBR_CAL(){

    extendingOrRetracting = true;

    axisBL.stop();
    axisBR.stop();
    axisBR.setTarget(axisBR.getPosition());

    bool axisBRDone = false;

    float BRDist = .001;
    
    while(!axisBRDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        
        if(axisBR.getCurrent() > currentThreshold || axisBRDone){
            axisBRDone = true;
        }
        else{
            axisBR.setTarget(axisBR.getPosition() - BRDist);
            BRDist = min(MEASUREMENTSPEED, BRDist + .001);
        }

        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisBL.updateEncoderPosition();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
            (*_sys_rt)();
        }
    }
    axisBR.setTarget(axisBR.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();


    extendingOrRetracting = false;

    return;
}

//Retract the lower left belt
void Maslow_::retractBL_CAL(){

    extendingOrRetracting = true;

    axisBL.stop();
    axisBR.stop();
    axisBL.setTarget(axisBL.getPosition());

    bool axisBLDone = false;

    float BLDist = .001;
    
    while(!axisBLDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        
        if(axisBL.getCurrent() > currentThreshold || axisBLDone){
            axisBLDone = true;
        }
        else{
            axisBL.setTarget(axisBL.getPosition() - BLDist);
            BLDist = min(MEASUREMENTSPEED, BLDist + .001); //Constrain the amount to move to .01
        }

        //These are needed because the flag will prevent regular PID recomputation
        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisBL.updateEncoderPosition();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
            (*_sys_rt)();
        }
    }
    axisBL.setTarget(axisBL.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();

    extendingOrRetracting = false;

    return;
}

//Reposition the sled without knowing the machine dimensions DOESNT WORK
void Maslow_::moveWithSlack(float x, float y, bool leftBelt, bool rightBelt){

    extendingOrRetracting = true;
    
    //The distance we need to move is the current position minus the target position
    double TLDist = axisTL.getPosition() - computeTL(x,y,0);
    double TRDist = axisTR.getPosition() - computeTR(x,y,0);
    
    //Record which direction to move
    double TLDir  = constrain(TLDist, -1, 1);
    double TRDir  = constrain(TRDist, -1, 1);
    
    double stepSize = .25;
    
    //Only use positive dist for incrementing counter (float int conversion issue?)
    TLDist = abs(TLDist);
    TRDist = abs(TRDist);
    
    //Make the lower arms compliant and move retract the other two until we get to the target distance
    
    unsigned long timeLastMoved1 = millis();
    unsigned long timeLastMoved2 = millis();
    double lastPosition1 = axisBL.getPosition();
    double lastPosition2 = axisBR.getPosition();
    double amtToMove1 = 1;
    double amtToMove2 = 1;


    double tlFullStep = stepSize*TLDir;
    double trFullStep = stepSize*TRDir;

    double TLTarget = axisTL.getTarget();
    double TRTarget = axisTR.getTarget();
    
    while(TLDist > 0 || TRDist > 0){
        
        //Set the lower axis to be compliant (if we should be). PID is recomputed in comply()
        if(leftBelt){
            //axisBL.comply(&timeLastMoved1, &lastPosition1, &amtToMove1, 3);
        }
        else{
            axisBL.stop();
        }

        if(rightBelt){
            //axisBR.comply(&timeLastMoved2, &lastPosition2, &amtToMove2, 3);
        }
        else{
            axisBR.stop();
        }
        
        // grbl_sendf( "BRPos: %f, BRamt: %f, BRtime: %l\n", lastPosition2, amtToMove2, timeLastMoved2);
        
        //Move the upper axis one step
        if(TLDist > 0){
            TLDist = TLDist - stepSize;
            TLTarget = TLTarget - tlFullStep;
            axisTL.setTarget(TLTarget);
        }
        if(TRDist > 0){
            TRDist = TRDist - stepSize;
            TRTarget = TRTarget - trFullStep;
            axisTR.setTarget(TRTarget);
        }

        //Makes the top axis actually move
        axisTR.recomputePID();
        axisTL.recomputePID();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisBL.updateEncoderPosition();
        (*_sys_rt)();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
        }
    }
    
    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());
    axisTR.setTarget(axisTR.getPosition());
    axisTL.setTarget(axisTL.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
    
    //Take up the internal slack to remove any slop between the spool and roller
    takeUpInternalSlack();

    extendingOrRetracting = false;
}

//This function removes any slack in the belt between the spool and the roller. 
//If there is slack there then when the motor turns the belt won't move which triggers the
//current threshold on pull tight too early. It only does this for the bottom axis.
void Maslow_::takeUpInternalSlack(){

    //Set the target to be .5mm in
    axisBL.setTarget(axisBL.getPosition() - 0.5);
    axisBR.setTarget(axisBR.getPosition() - 0.5);

    //Setup flags
    bool blDone = false;
    bool brDone = false;

    //Position hold until both axis are able to pull in until 
    while(!blDone && !brDone){

        //Check if they have pulled in fully
        if(axisBL.getPosition() < axisBL.getTarget()){
            blDone = true;
        }
        if(axisBR.getPosition() < axisBR.getTarget()){
            brDone = true;
        }

        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisBL.updateEncoderPosition();

        (*_sys_rt)(); //This keeps the wifi on and whatnot

        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
        }
    }

    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
}

//Updates where the center x and y positions are
void Maslow_::updateCenterXY(){
    
    double A = (trY - blY)/(trX-blX);
    double B = (brY-tlY)/(brX-tlX);
    centerX = (brY-(B*brX)+(A*trX)-trY)/(A-B);
    centerY = A*(centerX - trX) + trY;
    
}

Maslow_ &Maslow_::getInstance() {
  static Maslow_ instance;
  return instance;
}


Maslow_ &Maslow = Maslow.getInstance();