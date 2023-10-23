#include "MotorUnit.h"
#include "../Report.h"
#include "Maslow.h"

#define P 300 //260
#define I 0
#define D 0




void MotorUnit::begin(int forwardPin,
               int backwardPin,
               int readbackPin,
               int encoderAddress,
               int channel1,
               int channel2){

    _encoderAddress = encoderAddress;

    Maslow.I2CMux.setPort(_encoderAddress);
    encoder.begin();
    zero();

    motor.begin(forwardPin, backwardPin, readbackPin, channel1, channel2);

    positionPID.setPID(P,I,D);
    positionPID.setOutputLimits(-1023,1023);

    
}

void MotorUnit::zero(){
    Maslow.I2CMux.setPort(_encoderAddress);
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
 *  @brief  Reads the current position of the axis
 */
double MotorUnit::getPosition(){
    double positionNow = (mostRecentCumulativeEncoderReading/4096.0)*_mmPerRevolution*-1;
    return positionNow;
}

/*!
 *  @brief  Gets the current motor power draw
 */
double MotorUnit::getCurrent(){
    return motor.readCurrent();
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
bool MotorUnit::updateEncoderPosition(){

    if( !Maslow.I2CMux.setPort(_encoderAddress) ) return false;

    if(encoder.isConnected()){
        mostRecentCumulativeEncoderReading = encoder.getCumulativePosition(); //This updates and returns the encoder value
        return true;
    }
    else if(millis() - encoderReadFailurePrintTime > 5000){
        encoderReadFailurePrintTime = millis();
        log_info("Encoder read failure on " << _encoderAddress);
    }
    return false;
}

/*!
 *  @brief  Recomputes the PID and drives the output
 */
double MotorUnit::recomputePID(){
    
    _commandPWM = positionPID.getOutput(getPosition(),setpoint);

    motor.runAtPWM(_commandPWM);

    return _commandPWM;

}

/*!
 *  @brief  Runs the motor to extend for a little bit to put some slack into the coiled belt. Used to make it easier to extend. Now non-blocking. 
 */
void MotorUnit::decompressBelt(){
    // unsigned long time = millis();
    // unsigned long elapsedTime = millis()-time;
    // while(elapsedTime < 500){
    //     elapsedTime = millis()-time;
        motor.fullOut();
//    }
}

void MotorUnit::reset(){
    retract_speed = 0;
    retract_baseline = 700;
    incrementalThresholdHits = 0;
    amtToMove = 0.1;
    lastPosition = getPosition();
}
/*!
 *  @brief  Sets the motor to comply with how it is being pulled, non-blocking. 
 */
bool MotorUnit::comply( double maxSpeed){

    //Call it every 50 ms
    if(millis() - lastCallToComply < 50){
        return true;
    }
    //Update position and PID loop
    recomputePID();

    //If we've moved any, then drive the motor outwards to extend the belt
    float positionNow = getPosition();
    float distMoved = positionNow - lastPosition;

    //If the belt is moving out, let's keep it moving out
    if( distMoved > .001){
        //Increment the target
        setTarget(positionNow + amtToMove);
        
        amtToMove = amtToMove + 1;
        
        amtToMove = min(amtToMove, maxSpeed);
        
        //Reset the last moved counter
        //*timeLastMoved = millis();
    
    //If the belt is moving in we need to stop it from moving in
    }else if(distMoved < -.04){
        amtToMove = 0;
        setTarget(positionNow + .1);
        stop();
    }
    //Finally if the belt is not moving we want to spool things down
    else{
        amtToMove = amtToMove / 2;
        setTarget(positionNow);
        stop();
    }
    

    lastPosition = positionNow;

    //Return indicates if we have moved within the timeout threshold
    // if(millis()-*timeLastMoved > 5000){
    //     return false;
    // }
    //else{
        lastCallToComply = millis();
        return true;
    //}
}

/*!
 *  @brief  Fully retracts this axis and zeros it, non-blocking, returns true when done
 */
bool MotorUnit::retract(){

        //Gradually increase the pulling speed
        if(random(0,2) == 1){ //This is a hack to make it speed up more slowly because we can't add less than 1 to an int
            retract_speed = min(retract_speed + 1, 1023);
        }
        motor.backward(retract_speed);

        //When taught
        int currentMeasurement = motor.readCurrent();

        retract_baseline = alpha * float(currentMeasurement) + (1-alpha) * retract_baseline;

        if(currentMeasurement - retract_baseline > incrementalThreshold){
            incrementalThresholdHits = incrementalThresholdHits + 1;
        }
        else{
            incrementalThresholdHits = 0;
        }
        //EXPERIMENTAL, added because my BR current sensor is faulty, but might be an OK precaution
        //monitor the position change speed  
        bool beltStalled = false;
        if(retract_speed > 350 && (beltSpeedCounter++ % 10 == 0) ){ // skip the start, might create problems if the belt is slackking a lot
            beltSpeed = (getPosition() - lastPosition)*100 / (millis() - beltSpeedTimer);
            beltSpeedTimer = millis();
            lastPosition = getPosition();
            if(abs(beltSpeed) < 0.01){
                beltStalled = true;
            }
        }
        
        //log speed and current:
        if(currentMeasurement > absoluteCurrentThreshold || incrementalThresholdHits > 2 || beltStalled){  //changed from 4 to 2 to prevent overtighting
            //stop motor, reset variables
            motor.stop();
            retract_speed = 0;
            retract_baseline = 700;
            //Print how much the length of the belt changed compared to memory, log belt speed and current 
            log_info("Belt speed: " << beltSpeed << " mm/ms");
            log_info("Motor current: " << currentMeasurement);
            log_info("Belt positon after retract: ");
            log_info(getPosition());
            zero();
            return true;
        }
        else{
            return false;
        }
            
}
// extends the belt to the target length until it hits the target length, returns true when target length is reached
bool MotorUnit::extend(double targetLength) {

            unsigned long timeLastMoved = millis();

            if  (getPosition() < targetLength) {
                comply(500); //Comply does the actual moving
                return false;
            }
            //If reached target position, Stop and return
            setTarget(getPosition());
            motor.stop();

            //Position hold for 2 seconds to make sure we are in the right place - do we need this?
            // setTarget(targetLength);
            // time        = millis();
            // elapsedTime = millis() - time;
            // while (elapsedTime < 500) {
            //     elapsedTime = millis() - time;
            //     recomputePID();
            //     updateEncoderPosition();
            // }
            log_info("Belt positon after extend: ");
            log_info(getPosition());
            return true;
}