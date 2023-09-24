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

int lowerBeltsExtra = 2;
int callsSinceDelay = 0;

void Maslow_::begin(void (*sys_rt)()) {
  initialized = 1;

  Serial.begin(115200);

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

  tlX = -1.3429768202696968;
  tlY =  2064.1646310196916;
  tlZ = 116 + 38 + 20;
  trX = 2948.3849887926963; 
  trY = 2064.2064050836025;
  trZ = 69 + 38 + 20;
  blX = 0;
  blY = 0;
  blZ = 47 + 38 + 20;
  brX = 2957.3642166175055;
  brY = 0;
  brZ = 89 + 38 + 20;

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

}

void printToWeb (double precision){
    Serial.print( "Calibration Precision: ");
    Serial.println(precision);

    //log_info( "Calibration Precision: " + String(precision) +"mm");
}

void Maslow_::readEncoders() {
  axisTL.readEncoder();
  axisTR.readEncoder();
  axisBL.readEncoder();
  axisBR.readEncoder();
}

void Maslow_::home(int axis) {

    switch(axis) {
        case 0: //Bottom left
            extendingOrRetracting = true;
            axisBLHomed = axisBL.retract(computeBL(0, 300, 0));
            extendingOrRetracting = false;
            break;
        case 1: //Top Left
            if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
                lowerBeltsExtra = lowerBeltsExtra - 1;
                log_info("Extra: " << lowerBeltsExtra);
            }
            else{
                extendingOrRetracting = true;
                axisTLHomed = axisTL.retract(computeTL(0, 0, 0));
                extendingOrRetracting = false;
            }
            break;
        case 2: //Top right
            if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
                lowerBeltsExtra = lowerBeltsExtra + 1;
                log_info("Extra: " << lowerBeltsExtra);
            }
            else{
                extendingOrRetracting = true;
                axisTRHomed = axisTR.retract(computeTR(0, 0, 0));
                extendingOrRetracting = false;
            }
            break;
        case 4: //Bottom right
            if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
                runCalibration();
            }
            else {
                extendingOrRetracting = true;
                axisBRHomed = axisBR.retract(computeBR(0, 300, 0));
                extendingOrRetracting = false;
            }
            break;
        default:
            log_info("Unrecognized axis");
            break;
    }

    if(axisBLHomed && axisBRHomed && axisTRHomed && axisTLHomed) {
        log_info("All axis ready.\n");
    }
}

//Updates where the center x and y positions are
void Maslow_::updateCenterXY(){
    
    double A = (trY - blY)/(trX-blX);
    double B = (brY-tlY)/(brX-tlX);
    centerX = (brY-(B*brX)+(A*trX)-trY)/(A-B);
    centerY = A*(centerX - trX) + trY;
    
}

//Called from protocol.cpp
void Maslow_::recomputePID(){

    if(!initialized){ //If we haven't initialized we don't want to try to compute things because the PID controllers cause the processor to crash
        return;
    }

    int timeSinceLastCall = millis() - lastCallToPID;
    
    if(timeSinceLastCall > 20){
        int elapsedTimeLastMiss = millis() - lastMiss;
        log_info( "PID not being called often enough. Time since last call: " << timeSinceLastCall << " # since last miss:: " << callsSinceDelay << " ms ago: " << elapsedTimeLastMiss);
        callsSinceDelay = 0;
        lastMiss = millis();
    }
    else{
        callsSinceDelay++;
    }

    lastCallToPID = millis();

    // if(sys.state() != State::Idle && sys.state() != State::Alarm){
    //     float axisTLError = axisTL.getError();
    //     if(abs(axisTLError) > 0.5 && axisTLError < 10){
    //         log_info("TL Error: " << axisTLError);
    //     }
    //     float axisTRError = axisTR.getError();
    //     if(abs(axisTRError) > 0.5 && axisTRError < 10){
    //         log_info("TR Error: " << axisTRError);
    //     }
    //     float axisBLError = axisBL.getError();
    //     if(abs(axisBLError) > 0.5 && axisBLError < 10){
    //         log_info("TL Error: " << axisBLError);
    //     }
    //     float axisBRError = axisBR.getError();
    //     if(abs(axisBRError) > 0.5 && axisBRError < 10){
    //         log_info("TL Error: " << axisBRError);
    //     }
    // }

    //We always update the encoder positions
    axisTR.updateEncoderPosition();
    axisTL.updateEncoderPosition();
    axisBL.updateEncoderPosition();
    axisBR.updateEncoderPosition();

    
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
    xTarget = xTarget*1.01010101010101;
    yTarget = yTarget*0.997782705652873;
    
    if(!calibrationInProgress){

        computeTensions(xTarget, yTarget);

        axisBL.setTarget(computeBL(xTarget, yTarget, zTarget));
        axisBR.setTarget(computeBR(xTarget, yTarget, zTarget));
        axisTR.setTarget(computeTR(xTarget, yTarget, zTarget));
        axisTL.setTarget(computeTL(xTarget, yTarget, zTarget));
    }
}

void Maslow_::printMeasurementSet(float allLengths[][4]){

    //log_info("{bl:" + String(allLengths[0][0]) + ",   br:" + String(allLengths[0][1]) + ",   tr:" + String(allLengths[0][2]) + ",   tl:" + String(allLengths[0][3]) + "}");
    //log_info("{bl:" + String(allLengths[1][0]) + ",   br:" + String(allLengths[1][1]) + ",   tr:" + String(allLengths[1][2]) + ",   tl:" + String(allLengths[1][3]) + "}");
    //log_info("{bl:" + String(allLengths[2][0]) + ",   br:" + String(allLengths[2][1]) + ",   tr:" + String(allLengths[2][2]) + ",   tl:" + String(allLengths[2][3]) + "}");
    //log_info("{bl:" + String(allLengths[3][0]) + ",   br:" + String(allLengths[3][1]) + ",   tr:" + String(allLengths[3][2]) + ",   tl:" + String(allLengths[3][3]) + "}");

    (*_sys_rt)();
        
    // Delay without blocking
    unsigned long time = millis();
    unsigned long elapsedTime = millis()-time;
    while(elapsedTime < 250){
        elapsedTime = millis()-time;
        (*_sys_rt)();
    }
}

//Runs the calibration sequence to determine the machine's dimensions
void Maslow_::runCalibration(){
    
    //log_info( "Beginning calibration\n");
    Serial.println("Beginning calibration");
    
    calibrationInProgress = true;
    
    //Undoes any calls by the system to move these to (0,0)
    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());
    axisTR.setTarget(axisTR.getPosition());
    axisTL.setTarget(axisTL.getPosition());
    

    float allLengths1[5][4] = {0};
    float allLengths2[5][4] = {0};
    float allLengths3[5][4] = {0};
    float allLengths4[5][4] = {0};
    float allLengths5[5][4] = {0};
    float allLengths6[5][4] = {0};
    float allLengths7[5][4] = {0};
    float allLengths8[5][4] = {0};
    float allLengths9[5][4] = {0};
    float allLengths10[5][4] = {0};
    float allLengths11[5][4] = {0};
    float allLengths12[5][4] = {0};
    float allLengths13[5][4] = {0};
    float allLengths14[5][4] = {0};
    float allLengths15[5][4] = {0};
    float allLengths16[5][4] = {0};
    float allLengths17[5][4] = {0};
    float allLengths18[5][4] = {0};
    float allLengths19[5][4] = {0};
    float allLengths20[5][4] = {0};
    float allLengths21[5][4] = {0};
    float allLengths22[5][4] = {0};
    float allLengths23[5][4] = {0};
    float allLengths24[5][4] = {0};
    float allLengths25[5][4] = {0};

    //---------------------------------------------------Take measurements----------------------------
    

    //-----------------------------First column------------------------------

    //First measurement
    moveWithSlack(-800, 400, true, true);
    retractBL(); //Retract the bottom left belt first to tension the system
    retractBL();
    takeMeasurementAvgWithCheck(allLengths1);
    
    //Second measurmement
    moveWithSlack(-800, 200, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths2);

    //Third measurmement
    moveWithSlack(-800, 0, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths3);

    //Fourth measurement
    moveWithSlack(-800, -200, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths4);
    
    //Fifth measurement
    moveWithSlack(-800, -400, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths5);
    

    //------------------------------Second column--------------------------------

    //First measurement
    lowerBeltsGoSlack();
    lowerBeltsGoSlack();
    moveWithSlack(-400, 400, true, true);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths6);
    
    //Second measurement
    moveWithSlack(-400, 200, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths7);

    //Third measurement
    moveWithSlack(-400, 0, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths8);

    //Forth measurement
    moveWithSlack(-400, -200, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths9);
    
    //Fifth measurement
    moveWithSlack(-400, -400, false, false);
    retractBL();
    retractBL();
    takeMeasurementAvgWithCheck(allLengths10);

    //------------------------------Third column--------------------------------

    //First measurement
    lowerBeltsGoSlack();
    lowerBeltsGoSlack();
    moveWithSlack(0, 400, true, true);
    takeMeasurementAvgWithCheck(allLengths11);
    
    //Second measurement
    moveWithSlack(0, 200, false, false);
    takeMeasurementAvgWithCheck(allLengths12);

    //Third measurement
    moveWithSlack(0, 0, false, false);
    takeMeasurementAvgWithCheck(allLengths13);

    //Forth measurement
    moveWithSlack(0, -200, false, false);
    takeMeasurementAvgWithCheck(allLengths14);
    
    //Fifth measurement
    moveWithSlack(0, -400, false, false);
    takeMeasurementAvgWithCheck(allLengths15);

//----------------------Forth column---------------------------------
    
    //First measurement
    lowerBeltsGoSlack();
    lowerBeltsGoSlack();
    moveWithSlack(400, 400, true, true);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths16);
    
    //Second measurement
    moveWithSlack(400, 200, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths17);

    //Thrid measurement
    moveWithSlack(400, 0, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths18);

    //Fourth measurement
    moveWithSlack(400, -200, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths19);
    
    //Fifth measurement
    moveWithSlack(400, -400, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths20);

    //----------------------Fifth column---------------------------------
    
    //First measurement
    lowerBeltsGoSlack();
    lowerBeltsGoSlack();
    moveWithSlack(800, 400, true, true);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths21);
    
    //Second measurement
    moveWithSlack(800, 200, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths22);

    //Thrid measurement
    moveWithSlack(800, 0, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths23);

    //Fourth measurement
    moveWithSlack(800, -200, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths24);
    
    //Fifth measurement
    moveWithSlack(800, -400, false, false);
    retractBR();
    retractBR();
    takeMeasurementAvgWithCheck(allLengths25);


    //----------------------End of measurements---------------------------

    //Print out the resulting measurements, sometimes this crashes everything :/
    //log_info("Printing all measurements: ");
    printMeasurementSet(allLengths1);
    printMeasurementSet(allLengths2);
    printMeasurementSet(allLengths3);
    printMeasurementSet(allLengths4);
    printMeasurementSet(allLengths5);
    printMeasurementSet(allLengths6);
    printMeasurementSet(allLengths7);
    printMeasurementSet(allLengths8);
    printMeasurementSet(allLengths9);
    printMeasurementSet(allLengths10);
    printMeasurementSet(allLengths11);
    printMeasurementSet(allLengths12);
    printMeasurementSet(allLengths13);
    printMeasurementSet(allLengths14);
    printMeasurementSet(allLengths15);
    printMeasurementSet(allLengths16);
    printMeasurementSet(allLengths17);
    printMeasurementSet(allLengths18);
    printMeasurementSet(allLengths19);
    printMeasurementSet(allLengths20);
    printMeasurementSet(allLengths21);
    printMeasurementSet(allLengths23);
    printMeasurementSet(allLengths24);
    printMeasurementSet(allLengths25);
    
    
    lowerBeltsGoSlack();
    lowerBeltsGoSlack();
    moveWithSlack(0, 0, true, true);  //Go back to the center. This will pull the lower belts tight too
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
    
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
    
    // //log_info( "After computing calibration " + String(results[5]));
    
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
    
    //For safety we should pull tight here and verify that the results are basically what we expect before handing things over to the controller.
    float allLengths[5][4];
    takeMeasurementAvg(allLengths);
    takeMeasurementAvg(allLengths);
    
    double blError = (allLengths[3][0]-(_beltEndExtension+_armLength))-computeBL(0,0,0);
    double brError = (allLengths[3][1]-(_beltEndExtension+_armLength))-computeBR(0,0,0);
    
    //log_info( "Lower belt length mismatch: " + String(blError) + ", " +String(brError));
    
    calibrationInProgress = false;
    log_info( "Calibration finished");
    
}

void Maslow_::printMeasurements(float lengths[]){
    //log_info( "{bl:" + String(lengths[0]) + ",   br:" + String(lengths[1]) + ",   tr:" + String(lengths[2]) + ",   tl:" + String(lengths[3]) + "}");
}

void Maslow_::lowerBeltsGoSlack(){
    //log_info( "Lower belts going slack");
    
    unsigned long startTime = millis();

    axisBL.setTarget(axisBL.getPosition() + 2);
    axisBR.setTarget(axisBR.getPosition() + 2);
    
    while(millis()- startTime < 600){
        
        //Hold position
        axisBL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();

        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();

        (*_sys_rt)();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
        }
    }

    //Then stop them before moving on
    startTime = millis();

    axisBL.setTarget(axisBL.getPosition());
    axisBR.setTarget(axisBR.getPosition());

    while(millis()- startTime < 600){
        
        axisBL.updateEncoderPosition();
        axisBR.updateEncoderPosition();
        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();

        axisBL.recomputePID();
        axisBR.recomputePID();
        axisTR.recomputePID();
        axisTL.recomputePID();

        (*_sys_rt)();

    }

    //grbl_sendf( "Going slack completed\n");
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
void Maslow_::takeMeasurementAvgWithCheck(float allLengths[][4]){
    //log_info( "Beginning takeMeasurementAvg\n");
    Serial.println( "Beginning takeMeasurementAvg\n");
    float threshold = 0.9;
    while(true){
        float repeatability = takeMeasurementAvg(allLengths);
        if(repeatability < threshold){
            //log_info( "Using measurement with precision:");
            //log_info(repeatability);
            break;
        }
        //log_info( "Repeating measurement\n");
    }
}

// Takes 5 measurements and returns how consistent they are
float Maslow_::takeMeasurementAvg(float allLengths[][4]) {
    //log_info("Beginning to take averaged measurement.\n");
    Serial.println("Beginning to take averaged measurement.");

    // Where our five measurements will be stored
    float lengths1[4];
    float lengths2[4];
    float lengths3[4];
    float lengths4[4];
    float lengths5[4];

    //Where the average lengths will be stored
    float avgLengths[4];

    takeMeasurement(lengths1);
    lowerBeltsGoSlack();
    takeMeasurement(lengths1);  // Repeat the first measurement to discard the one before everything was pulled taught
    lowerBeltsGoSlack();
    takeMeasurement(lengths2);
    lowerBeltsGoSlack();
    takeMeasurement(lengths3);
    lowerBeltsGoSlack();
    takeMeasurement(lengths4);
    lowerBeltsGoSlack();
    takeMeasurement(lengths5);

    allLengths[0][0] = lengths1[0];
    allLengths[0][1] = lengths1[1];
    allLengths[0][2] = lengths1[2];
    allLengths[0][3] = lengths1[3];
    allLengths[1][0] = lengths2[0];
    allLengths[1][1] = lengths2[1];
    allLengths[1][2] = lengths2[2];
    allLengths[1][3] = lengths2[3];
    allLengths[2][0] = lengths3[0];
    allLengths[2][1] = lengths3[1];
    allLengths[2][2] = lengths3[2];
    allLengths[2][3] = lengths3[3];
    allLengths[3][0] = lengths4[0];
    allLengths[3][1] = lengths4[1];
    allLengths[3][2] = lengths4[2];
    allLengths[3][3] = lengths4[3];
    allLengths[4][0] = lengths5[0];
    allLengths[4][1] = lengths5[1];
    allLengths[4][2] = lengths5[2];
    allLengths[4][3] = lengths5[3];

    avgLengths[0] = (lengths1[0] + lengths2[0] + lengths3[0] + lengths4[0] + lengths5[0]) / 5.0;
    avgLengths[1] = (lengths1[1] + lengths2[1] + lengths3[1] + lengths4[1] + lengths5[1]) / 5.0;
    avgLengths[2] = (lengths1[2] + lengths2[2] + lengths3[2] + lengths4[2] + lengths5[2]) / 5.0;
    avgLengths[3] = (lengths1[3] + lengths2[3] + lengths3[3] + lengths4[3] + lengths5[3]) / 5.0;

    float m1 = printMeasurementMetrics(avgLengths[0], lengths1[0], lengths2[0], lengths3[0], lengths4[0], lengths5[0]);
    float m2 = printMeasurementMetrics(avgLengths[1], lengths1[1], lengths2[1], lengths3[1], lengths4[1], lengths5[1]);
    float m3 = printMeasurementMetrics(avgLengths[2], lengths1[2], lengths2[2], lengths3[2], lengths4[2], lengths5[2]);
    float m4 = printMeasurementMetrics(avgLengths[3], lengths1[3], lengths2[3], lengths3[3], lengths4[3], lengths5[3]);

    float maxDeviation = std::max({m1, m2, m3, m4});

    //log_info("Max Deviation: " + String(maxDeviation));

    return maxDeviation;
}

//Retract the lower belts until they pull tight and take a measurement
void Maslow_::takeMeasurement(float lengths[]){
    //log_info( "Taking a measurement.\n");

    axisBL.stop();
    axisBR.stop();

    bool axisBLDone = false;
    bool axisBRDone = false;

    float BLDist = .01;
    float BRDist = .01;
    
    while(!axisBLDone || !axisBRDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        int currentThreshold = 1500;
        
        if(axisBL.getCurrent() > currentThreshold || axisBLDone){  //Check if the current threshold is hit
            axisBLDone = true;
        }
        else{                                                       //If not
            axisBL.setTarget(axisBL.getPosition() - BLDist);                  //Pull in a little more
            BLDist = min(0.2, BLDist + .01);                                     //Slowly ramp up the speed
        }
        
        if(axisBR.getCurrent() > currentThreshold || axisBRDone){
            if(axisBRDone == false){
                //log_info( "BR Trip Current: " + String(axisBR.getCurrent()) + "\n");
            }
            axisBRDone = true;
        }
        else{
            axisBR.setTarget(axisBR.getPosition() - BRDist);
            BRDist = min(0.2, BRDist + .01);
        }
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 25){
            elapsedTime = millis()-time;
            recomputePID();  //This recomputes the PID four all four servos
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
    
    //log_info("Measurement finished");
    //log_info("{bl:" + String(lengths[0]) + ", br:" + String(lengths[1]) + ",   tr:" + String(lengths[2]) + ",  tl:" + String(lengths[3]) + "}");
    //log_info( "Measured:\n%f, %f \n%f %f \n",lengths[3], lengths[2], lengths[0], lengths[1]);
    
    return;
}

//Retract the lower right belt
void Maslow_::retractBR(){

    axisBL.stop();
    axisBR.stop();

    bool axisBRDone = false;

    float BRDist = .01;
    
    while(!axisBRDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        int currentThreshold = 1500;
        
        if(axisBR.getCurrent() > currentThreshold || axisBRDone){
            if(axisBRDone == false){
                //log_info( "BR Trip Current: " + String(axisBR.getCurrent()) + "\n");
            }
            axisBRDone = true;
        }
        else{
            axisBR.setTarget(axisBR.getPosition() - BRDist);
            BRDist = min(0.2, BRDist + .01);
        }
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 25){
            elapsedTime = millis()-time;
            recomputePID();  //This recomputes the PID four all four servos
            (*_sys_rt)();
        }
    }
    axisBR.setTarget(axisBR.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();

    return;
}

//Retract the lower left belt
void Maslow_::retractBL(){

    axisBL.stop();
    axisBR.stop();

    bool axisBLDone = false;

    float BLDist = .01;
    
    while(!axisBLDone){  //As long as one axis is still pulling
        
        //If any of the current values are over the threshold then stop and exit, otherwise pull each axis a little bit tighter by incrementing the target position
        int currentThreshold = 1500;
        
        if(axisBL.getCurrent() > currentThreshold || axisBLDone){
            if(axisBLDone == false){
                //log_info( "BL Trip Current: " + String(axisBL.getCurrent()) + "\n");
            }
            axisBLDone = true;
        }
        else{
            axisBL.setTarget(axisBL.getPosition() - BLDist);
            BLDist = min(0.2, BLDist + .01);
        }
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 25){
            elapsedTime = millis()-time;
            recomputePID();  //This recomputes the PID four all four servos
            (*_sys_rt)();
        }
    }
    axisBL.setTarget(axisBL.getPosition());
    
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();

    return;
}

//Reposition the sled without knowing the machine dimensions
void Maslow_::moveWithSlack(float x, float y, bool leftBelt, bool rightBelt){
    
    log_info( "Moving to with slack");

    extendingOrRetracting = true;
    
    //The distance we need to move is the current position minus the target position
    double TLDist = axisTL.getPosition() - computeTL(x,y,0);
    double TRDist = axisTR.getPosition() - computeTR(x,y,0);
    
    //Record which direction to move
    double TLDir  = constrain(TLDist, -1, 1);
    double TRDir  = constrain(TRDist, -1, 1);
    
    double stepSize = .15;
    
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
    
    while(TLDist > 0 || TRDist > 0){
        
        //Set the lower axis to be compliant (if we should be). PID is recomputed in comply()
        if(leftBelt){
            axisBL.comply(&timeLastMoved1, &lastPosition1, &amtToMove1, 3);
        }
        else{
            axisBL.updateEncoderPosition();
            axisBL.stop();
        }

        if(rightBelt){
            axisBR.comply(&timeLastMoved2, &lastPosition2, &amtToMove2, 3);
        }
        else{
            axisBR.updateEncoderPosition();
            axisBR.stop();
        }
        
        // grbl_sendf( "BRPos: %f, BRamt: %f, BRtime: %l\n", lastPosition2, amtToMove2, timeLastMoved2);
        
        //Move the upper axis one step
        if(TLDist > 0){
            TLDist = TLDist - stepSize;
            axisTL.setTarget((axisTL.getTarget() - (stepSize*TLDir)));
        }
        if(TRDist > 0){
            TRDist = TRDist - stepSize;
            axisTR.setTarget((axisTR.getTarget() - (stepSize*TRDir)));
        }

        axisTR.updateEncoderPosition();
        axisTL.updateEncoderPosition();

        axisTR.recomputePID();
        axisTL.recomputePID();

        (*_sys_rt)();
        
        // Delay without blocking
        unsigned long time = millis();
        unsigned long elapsedTime = millis()-time;
        while(elapsedTime < 10){
            elapsedTime = millis()-time;
        }
    }
    
    //grbl_sendf( "Positional errors at the end of move <-%f, %f ->\n", axisTL.getError(), axisTR.getError());
    
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
    //log_info("Take up internal slack");
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

        recomputePID();
        (*_sys_rt)();

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

float Maslow_::computeVertical(float firstUpper, float firstLower, float secondUpper, float secondLower){
    //Derivation at https://math.stackexchange.com/questions/4090346/solving-for-triangle-side-length-with-limited-information
    
    float b = secondUpper;   //upper, second
    float c = secondLower; //lower, second
    float d = firstUpper; //upper, first
    float e = firstLower;  //lower, first

    float aSquared = (((b*b)-(c*c))*((b*b)-(c*c))-((d*d)-(e*e))*((d*d)-(e*e)))/(2*(b*b+c*c-d*d-e*e));

    float a = sqrt(aSquared);
    
    return a;
}

void Maslow_::computeFrameDimensions(float lengthsSet1[], float lengthsSet2[], float machineDimensions[]){
    //Call compute verticals from each side
    
    float leftHeight = computeVertical(lengthsSet1[3],lengthsSet1[0], lengthsSet2[3], lengthsSet2[0]);
    float rightHeight = computeVertical(lengthsSet1[2],lengthsSet1[1], lengthsSet2[2], lengthsSet2[1]);
    
    //log_info( "Computed vertical measurements:\n%f \n%f \n%f \n",leftHeight, rightHeight, (leftHeight+rightHeight)/2.0);
}

Maslow_ &Maslow_::getInstance() {
  static Maslow_ instance;
  return instance;
}

Maslow_ &Maslow = Maslow.getInstance();