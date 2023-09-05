#include "MotorUnit.h"
#include "../Report.h"


#define P 300 //260
#define I 35
#define D 0

#define TCAADDR 0x70

void tcaselect(uint8_t i) {
  if (i > 7) return;
 
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}

void MotorUnit::begin(int forwardPin,
               int backwardPin,
               int readbackPin,
               int encoderAddress,
               int channel1,
               int channel2){
    Serial.println("Beginning motor unit");

    _encoderAddress = encoderAddress;

    Wire.begin(5,4, 200000);
    tcaselect(_encoderAddress);
    encoder.begin();
    zero();

    motor.begin(forwardPin, backwardPin, readbackPin, channel1, channel2);

    positionPID.setPID(P,I,D);
    positionPID.setOutputLimits(-1023,1023);

    
}

void MotorUnit::readEncoder(){
    tcaselect(_encoderAddress);

    if(encoder.isConnected()){
        //log_info("Connected:");
        //log_info(_encoderAddress);
    } else {
        //log_info("Not connected:");
        //log_info(_encoderAddress);
    }
}

void MotorUnit::zero(){
    tcaselect(_encoderAddress);
    encoder.resetCumulativePosition();
}

/*!
 *  @brief  Sets the target location
 */
void MotorUnit::setTarget(double newTarget){
    setpoint = newTarget;
}

/*!
 *  @brief  Gets the target location
 */
double MotorUnit::getTarget(){
    return setpoint;
}

/*!
 *  @brief  Sets the position of the cable
 */
int MotorUnit::setPosition(double newPosition){
    int angleTotal = (newPosition*4096)/_mmPerRevolution;
    tcaselect(_encoderAddress);
    encoder.resetCumulativePosition(angleTotal);

    return true;
}

/*!
 *  @brief  Reads the current position of the axis
 */
double MotorUnit::getPosition(){
    return (mostRecentCumulativeEncoderReading/4096.0)*_mmPerRevolution*-1;
}

/*!
 *  @brief  Gets the current motor power draw
 */
double MotorUnit::getCurrent(){
    return motor.readCurrent();
}

/*!
 *  @brief  Computes and returns the error in the axis positioning
 */
double MotorUnit::getError(){
    
    double errorDist = setpoint - getPosition();
    
    return errorDist;
    
}

/*!
 *  @brief  Stops the motor
 */
void MotorUnit::stop(){
    motor.stop();
}

//---------------------Functions related to maintaining the PID controllers-----------------------------------------



/*!
 *  @brief  Reads the encoder value and updates it's position and measures the velocity since the last call
 */
void MotorUnit::updateEncoderPosition(){

    tcaselect(_encoderAddress);

    if(encoder.isConnected()){
        mostRecentCumulativeEncoderReading = encoder.getCumulativePosition(); //This updates and returns the encoder value
    }
    else if(!encoderReadFailurePrint){
        encoderReadFailurePrint = true;
        log_info("Encoder read failure on " << _encoderAddress);
    }
}

/*!
 *  @brief  Recomputes the PID and drives the output
 */
double MotorUnit::recomputePID(){
    
    double commandPWM = positionPID.getOutput(getPosition(),setpoint);

    motor.runAtPWM(commandPWM);

    // if(random(50) == 1){
    //     log_info("PWM: " + String(commandPWM));
    // }

    return commandPWM;

    //Read the motor current and check for stalls
    // double currentNow = getCurrent();
    // if(currentNow > _stallCurrent){
    //     _stallCount = _stallCount + 1;
    // }
    // else{
    //     _stallCount = 0;
    // }
    // if(_stallCount > _stallThreshold){
        // if(_axisID == 1){    
        //     _webPrint(0xFF,"BR stalled at current: %f\n", currentNow);
        // }
        // else if(_axisID == 3){    
        //     _webPrint(0xFF,"TR stalled at current: %f\n", currentNow);
        // }
        // else if(_axisID == 7){    
        //     _webPrint(0xFF,"BL stalled at current: %f\n", currentNow);
        // }
        // else if(_axisID == 9){    
        //     _webPrint(0xFF,"TL stalled at current: %f\n", currentNow);
        // }
        // else{    
        //     _webPrint(0xFF,"%i stalled at current: %f\n",_axisID, currentNow);
        // }
    //     _stallCount = 0;
    // }
    
    

    //Add some monitoring to the top right axis...this can crash the processor because it prints out so much data
    // if(_axisID == 3){
    //     _webPrint(0xFF,"TR PID: %f\n", commandPWM);
    // }

    // if(abs(getPosition() - setpoint ) > 5){
    //     _numPosErrors = _numPosErrors + 1;

        // if(_numPosErrors > 2){
        //     if(_axisID == 1){    
        //         _webPrint(0xFF,"BR position error of %fmm ", getPosition() - setpoint);
        //         _webPrint(0xFF,"BR current draw %i ", currentMeasurement);
        //         _webPrint(0xFF,"BR PID output %f\n", commandPWM);
        //     }
        //     else if(_axisID == 3){
        //         _webPrint(0xFF,"TR position error of %fmm ", getPosition() - setpoint);
        //         _webPrint(0xFF,"TR current draw %i ", currentMeasurement);
        //         _webPrint(0xFF,"TR PID output %f\n", commandPWM);
        //     }
        //     else if(_axisID == 7){
        //         _webPrint(0xFF,"BL position error of %fmm ", getPosition() - setpoint);
        //         _webPrint(0xFF,"BL current draw %i ", currentMeasurement);
        //         _webPrint(0xFF,"BL PID output %f\n", commandPWM);
        //     }
        //     else if(_axisID == 9){
        //         _webPrint(0xFF,"TL position error of %fmm ", getPosition() - setpoint);
        //         _webPrint(0xFF,"TL current draw %i ", currentMeasurement);
        //         _webPrint(0xFF,"TL PID output %f\n", commandPWM);
        //     }
        //     else{
        //         _webPrint(0xFF,"%i position error of %fmm\n",_axisID, getPosition() - setpoint);
        //     }
        // }
    //}
    // else{
    //     _numPosErrors = 0;
    // }

    //This code adds an offiset to remove the deadband. It needs to be tuned for the new 0-1023 values
    // if(commandPWM > 0){
    //     commandPWM = commandPWM + 7000;
    // }

    // if(commandPWM > 1023){
    //     commandPWM = 1023;
    // }

}

/*!
 *  @brief  Runs the motor to extend for a little bit to put some slack into the coiled belt. Used to make it easier to extend.
 */
void MotorUnit::decompressBelt(){
    unsigned long time = millis();
    unsigned long elapsedTime = millis()-time;
    while(elapsedTime < 500){
        elapsedTime = millis()-time;
        motor.fullOut();
        updateEncoderPosition();
    }
}

/*!
 *  @brief  Sets the motor to comply with how it is being pulled
 */
bool MotorUnit::comply(unsigned long *timeLastMoved, double *lastPosition, double *amtToMove, double maxSpeed){
    
    //Update position and PID loop
    recomputePID();
    
    //If we've moved any, then drive the motor outwards to extend the belt
    float positionNow = getPosition();
    float distMoved = positionNow - *lastPosition;

    Serial.print("Dist moved: ");
    Serial.print(distMoved);
    Serial.print("  Target: ");
    Serial.println(getTarget());

    //If the belt is moving out, let's keep it moving out
    if( distMoved > .001){
        //Increment the target
        setTarget(positionNow + *amtToMove);
        
        *amtToMove = *amtToMove + 1;
        
        *amtToMove = min(*amtToMove, maxSpeed);
        
        //Reset the last moved counter
        *timeLastMoved = millis();
    
    //If the belt is moving in we need to stop it from moving in
    }else if(distMoved < -.04){
        *amtToMove = 0;
        setTarget(positionNow + .1);
        stop();
    }
    //Finally if the belt is not moving we want to spool things down
    else{
        *amtToMove = *amtToMove / 2;
        setTarget(positionNow);
        stop();
    }
    

    *lastPosition = positionNow;

    //Return indicates if we have moved within the timeout threshold
    if(millis()-*timeLastMoved > 5000){
        return false;
    }
    else{
        return true;
    }
}

/*!
 *  @brief  Fully retracts this axis and zeros it out or if it is already retracted extends it to the targetLength
 */
bool MotorUnit::retract(double targetLength){
    
    Serial.println("Retracting");
    log_info("Retracting called within MotorUnit!");
    int absoluteCurrentThreshold = 1900;
    int incrementalThreshold = 75;
    int incrementalThresholdHits = 0;
    float alpha = .2;
    float baseline = 700;

    uint16_t speed = 0;

    //Keep track of the elapsed time
    unsigned long time = millis();
    unsigned long elapsedTime = 0;
    
    //Pull until taught
    while(true){
        
        //Gradually increase the pulling speed
        if(random(0,4) == 2){ //This is a hack to make it speed up more slowly because we can't add less than 1 to an int
            speed = min(speed + 1, 1023);
        }
        motor.backward(speed);

        updateEncoderPosition();
        //When taught
        int currentMeasurement = motor.readCurrent();

        Serial.print("Current: ");
        Serial.print(currentMeasurement);
        Serial.print("  Baseline: ");
        Serial.print(baseline);
        Serial.print("  Difference: ");
        Serial.print(currentMeasurement - baseline);
        Serial.print("  Hits: ");
        Serial.println(incrementalThresholdHits);

        //_webPrint(0xFF,"Current: %i, Baseline: %f, difference: %f \n", currentMeasurement, baseline, currentMeasurement - baseline);
        baseline = alpha * float(currentMeasurement) + (1-alpha) * baseline;

        if(currentMeasurement - baseline > incrementalThreshold){
            Serial.println("Dynamic thershold hit");
            //_webPrint(0xFF,"Dynamic threshold hit\n");
            incrementalThresholdHits = incrementalThresholdHits + 1;
        }
        else{
            incrementalThresholdHits = 0;
        }

        if(currentMeasurement > absoluteCurrentThreshold || incrementalThresholdHits > 4){
            motor.stop();

            //Print how much the length of the belt changed compared to memory
            //_webPrint(0xFF,"Belt position after retract: %f\n", getPosition());
            log_info("Belt positon after retract: ");
            log_info(getPosition());

            zero();
            
            //If we hit the current limit immediately because there wasn't any slack we will extend
            elapsedTime = millis()-time;
            if(elapsedTime < 1500){

                Serial.println("Immediate hit detected");
                
                //Extend some belt to get things started
                decompressBelt();

                Serial.println("After decompress belt");
                
                unsigned long timeLastMoved = millis();
                double lastPosition = getPosition();
                double amtToMove = 0.1;
                
                Serial.println("Got to the comply part");
                while(getPosition() < targetLength){
                    //Check for timeout
                    if(!comply(&timeLastMoved, &lastPosition, &amtToMove, 500)){//Comply updates the encoder position and does the actual moving
                        
                        //Stop and return
                        setTarget(getPosition());
                        motor.stop();
                        
                        return false;
                    }
                    
                    // Delay without blocking
                    unsigned long time = millis();
                    unsigned long elapsedTime = millis()-time;
                    while(elapsedTime < 50){
                        elapsedTime = millis()-time;
                    }
                }
                Serial.println("After the comply part");
                
                //Position hold for 2 seconds to make sure we are in the right place
                setTarget(targetLength);
                time = millis();
                elapsedTime = millis()-time;
                while(elapsedTime < 500){
                    elapsedTime = millis()-time;
                    recomputePID();
                }
                
                motor.stop();

                log_info("Belt positon after extend: ");
                log_info(getPosition());
                log_info("Expected measured: ");
                log_info(getPosition() + 153.4);
                return true;
            }
            else{
                return false;
            }
        }
    }
}
