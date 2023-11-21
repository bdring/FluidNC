
#include "Maslow.h"
#include "../Report.h"

// Maslow specific defines
#define TLEncoderLine 2
#define TREncoderLine 1
#define BLEncoderLine 3
#define BREncoderLine 0

//These are the values for the new board with etherent cables
//#define TLEncoderLine 2
//#define TREncoderLine 1
//#define BLEncoderLine 3
//#define BREncoderLine 0

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
int ENCODER_READ_FREQUENCY_HZ = 100;

int lowerBeltsExtra = 4;
int callsSinceDelay = 0;

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

#define HORIZONTAL 0
#define VERTICAL 1




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

  tlX =-3.127880538461895;
  tlY = 2063.1006937512166;
  tlZ = 116 + 38;
  trX = 2944.4878198392585; 
  trY = 2069.656171241167;
  trZ = 69 + 38;
  blX = 0;
  blY = 0;
  blZ = 47 + 38;
  brX = 2959.4124827780993;
  brY = 0;
  brZ = 89 + 38;

//   tlX = 5.5;
//   tlY = 2150;
//   tlZ = 0;
//   trX = 3135; 
//   trY = 2150;
//   trZ = 0;
//   blX = 0;
//   blY = 0;
//   blZ = 0;
//   brX = 3095;
//   brY = 0;
//   brZ = 0;


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

  currentThreshold = 1700;
  lastCallToUpdate = millis();
  orientation = VERTICAL;
  log_info("Starting Maslow v 1.00");
}

bool Maslow_::all_axis_homed(){
    return axis_homed[0] && axis_homed[1] && axis_homed[2] && axis_homed[3];
}

void Maslow_::home() {
  //run all the retract functions untill we hit the current limit
  if (retractingTL) {
      if (axisTL.retract()){
          retractingTL = false;
          axis_homed[0] = true;
      }
  }
  if (retractingTR) {
      if (axisTR.retract()){
          retractingTR = false;
          axis_homed[1] = true;
      }
  }
  if (retractingBL) {
      if (axisBL.retract()){
          retractingBL = false;
          axis_homed[2] = true;
      }
  }
  if (retractingBR) {
      if (axisBR.retract()){
          retractingBR = false;
          axis_homed[3] = true;
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
      if (millis() - complyCallTimer < 700) {
          if (millis() - complyCallTimer > 0)
              axisBR.decompressBelt();
          if (millis() - complyCallTimer > 150)
              axisBL.decompressBelt();
          if (millis() - complyCallTimer > 250)
              axisTR.decompressBelt();
          if (millis() - complyCallTimer > 350)
              axisTL.decompressBelt();
      } else {
          axisTL.comply(1000);  //call to recomputePID() inside here
          axisTR.comply(1000);
          axisBL.comply(1000);
          axisBR.comply(1000);
      }
  }
  
  // $CAL - calibration mode
  if(calibrationInProgress){
        calibration_loop();
  }

  //if we are done with all the homing moves, switch system state back to Idle?
  if (!retractingTL && !retractingBL && !retractingBR && !retractingTR && !extendingALL && !complyALL && !calibrationInProgress) {
      sys.set_state(State::Idle);
  }
}

bool Maslow_::take_measurement(int waypoint){
 if (orientation == VERTICAL) {
      //first we pull two bottom belts tight one after another, if x<0 we pull left belt first, if x>0 we pull right belt first
      static bool BL_tight = false;
      static bool BR_tight = false;
      axisTL.recomputePID();
      axisTR.recomputePID();  
      if (x < 0) {
          if (!BL_tight) {
              if (axisBL.pull_tight()) {
                  BL_tight = true;
                  //log_info("Pulled BL tight");
              }
              return false;
          }
          if (!BR_tight) {
              if (axisBR.pull_tight()) {
                  BR_tight = true;
                    //log_info("Pulled BR tight");
              }
              return false;
          }
      } 
      
      else {
          if (!BR_tight) {
              if (axisBR.pull_tight()) {
                  BR_tight = true;
                    //log_info("Pulled BR tight");
              }
              return false;
          }
          if (!BL_tight) {

              if (axisBL.pull_tight()) {
                  BL_tight = true;
                    //log_info("Pulled BL tight");
              }
              return false;
          }
      }

      //once both belts are pulled, take a measurement
      if (BR_tight && BL_tight) {
          //take measurement and record it to the calibration data array
          calibration_data[0][waypoint] = axisTL.getPosition();
          calibration_data[1][waypoint] = axisTR.getPosition();
          calibration_data[2][waypoint] = axisBL.getPosition();
          calibration_data[3][waypoint] = axisBR.getPosition();
          BR_tight = false;
          BL_tight = false;
          return true;
      }
      return false;
  }
  return false;
}
bool Maslow_::take_measurement_avg_with_check(int waypoint) {
  //take 5 measurements in a row, (ignoring the first one), if they are all within 1mm of each other, take the average and record it to the calibration data array
  static int           run                = 0;
  static double        measurements[4][4] = { 0 };
  static double        avg                = 0;
  static double        sum                = 0;
  static unsigned long decompressTimer    = millis();

//Decompress belts for 500ms...this happens by returnign right away before running any of the rest of the code
//   if (millis() - decompressTimer < 500) {
//       axisBL.decompressBelt();
//       axisBR.decompressBelt();
//       return false;
//   }

//Stop for 50ms
//   //we need to stop motors after decompression was finished once
//   else if (millis() - decompressTimer < 550) {
//       stopMotors();
//   }

  if (take_measurement(waypoint)) {
      if (run < 3) {
          //decompress lower belts for 500 ms before taking the next measurement
          decompressTimer = millis();
          run++;
          return false;  //discard the first three measurements
      }

      measurements[0][run - 3] = calibration_data[0][waypoint];  //-3 cuz discarding the first 3 measurements
      measurements[1][run - 3] = calibration_data[1][waypoint];
      measurements[2][run - 3] = calibration_data[2][waypoint];
      measurements[3][run - 3] = calibration_data[3][waypoint];

      run++;

      static int criticalCounter = 0;
      if (run > 6) {
          run = 0;

          //check if all measurements are within 1mm of each other
          static double maxDeviation[4] = { 0 };
          static double maxDeviationAbs = 0;
          for (int i = 0; i < 4; i++) {
              for (int j = 0; j < 3; j++) {
                    //find max deviation between measurements
                    maxDeviation[i] = max(maxDeviation[i], abs(measurements[i][j] - measurements[i][j + 1]));
              }
          }
          //log max deviations at every axis:
          //log_info("Max deviation at BL: " << maxDeviation[2] << " BR: " << maxDeviation[3] << " TR: " << maxDeviation[1] << " TL: " << maxDeviation[0]);
          //find max deviation between all measurements
          for (int i = 0; i < 4; i++) {
              maxDeviationAbs = max(maxDeviationAbs, maxDeviation[i]);
          }
          if (maxDeviationAbs > 1.5) {
              log_error("Measurement error, measurements are not within 1.5 mm of each other, trying again");
              //print all the measurements in readable form:
              for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        //use axis id to label:
                        log_info(axis_id_to_label(i).c_str() << " " << measurements[i][j]);
                    }
              }
              //reset the run counter to run the measurements again
              if (criticalCounter++ > 8) {
                    log_error("Critical error, measurements are not within 1.5mm of each other 8 times in a row, stopping calibration");
                    calibrationInProgress = false;
                    waypoint              = 0;
                    criticalCounter       = 0;
                    return false;
              }

              decompressTimer = millis();
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
          log_info("Took measurement at waypoint " << waypoint);
          return true;
      }

      //decompress lower belts for 500 ms before taking the next measurement
      decompressTimer = millis();
  }

  return false;
}

//function for outputting calibration data in the log line by line like this: {bl:2376.69,   br:923.40,   tr:1733.87,   tl:2801.87},
void Maslow_::print_calibration_data(){
    for(int i = 0; i < CALIBRATION_GRID_SIZE; i++){
        log_info("{bl:" << calibration_data[2][i] << ",   br:" << calibration_data[3][i] << ",   tr:" << calibration_data[1][i] << ",   tl:" << calibration_data[0][i] << "},");
    }
}

void Maslow_::calibration_loop(){

     static int waypoint = 0; 
        static bool measurementInProgress = false;
        //Taking measurment once we've reached the point
        if(measurementInProgress){
            if(take_measurement_avg_with_check(waypoint)){ //Takes a measurement and returns true if it's done
                
                measurementInProgress = false;
                waypoint++;                                 //Increment the waypoint counter

                if(waypoint > 98 ){ //If we have reached the end of the calibration process
                    calibrationInProgress = false;
                    waypoint = 0;
                    log_info("Calibration complete");
                    print_calibration_data();
                    sys.set_state(State::Idle);
                }
                else{ //Otherwise move to the next point
                    log_info("Moving from: " << calibrationGrid[waypoint-1][0] << " " << calibrationGrid[waypoint-1][1] << " to: " << calibrationGrid[waypoint][0] << " " << calibrationGrid[waypoint][1] << " direction: " << get_direction(calibrationGrid[waypoint-1][0], calibrationGrid[waypoint-1][1], calibrationGrid[waypoint][0], calibrationGrid[waypoint][1]));
                    move_with_slack(getTargetX(), getTargetY(), calibrationGrid[waypoint][0], calibrationGrid[waypoint][1]);
                }
            }
        }

        //travel to the start point
        else if(waypoint == 0){

            //pull bottom belts tight first, NOT
            // static bool BL_pulled = false;
            // static bool BR_pulled = false;
            
            // if(!BL_pulled){
            //     if(axisBL.pull_tight()){
            //         BL_pulled = true;
            //         log_info("Pulled BL tight");
            //     }
            //     else return;
            // }
            // if(!BR_pulled){
            //     if(axisBR.pull_tight()){
            //         BR_pulled = true;
            //         log_info("Pulled BR tight");
            //         log_info("Moving to the start point");
            //     }
            //     else return;
            // }

            //move to the start point
            
            if(move_with_slack(centerX,centerY, calibrationGrid[0][0], calibrationGrid[0][1])){
                measurementInProgress = true;
                log_info("arrived at the start point");
                x = calibrationGrid[0][0];
                y = calibrationGrid[0][1];
                hold(100);
            }

        }

        //perform the calibrartion steps in the grid
        else{
            
            if(move_with_slack(calibrationGrid[waypoint-1][0], calibrationGrid[waypoint-1][1], calibrationGrid[waypoint][0], calibrationGrid[waypoint][1])){
                measurementInProgress = true;
                x = calibrationGrid[waypoint][0];
                y = calibrationGrid[waypoint][1];
                hold(100);
            }

        }
}

void Maslow_::hold(unsigned long time){
    holdTime = time;
    holding = true;
    holdTimer = millis();
    //log_info("Holding for " << int(time) << "ms");
}

void Maslow_::generate_calibration_grid() {
  log_info("Generating calibration grid");

  int gridSizeX = 10;
  int gridSizeY = 9;
  int xSpacing = 175;
  int ySpacing = 100;
  int pointCount = 0;

  for(int i = -gridSizeX/2; i <= gridSizeX/2; i++) {
    if(i % 2 == 0) {
      for(int j = -gridSizeY/2; j <= gridSizeY/2; j++) {
        log_info("Point: " << pointCount << "(" << i * xSpacing << ", " << j * ySpacing << ")");
        calibrationGrid[pointCount][0] = i * xSpacing;
        calibrationGrid[pointCount][1] = j * ySpacing;
        pointCount++;
      }
    } else {
      for(int j = gridSizeY/2; j >= -gridSizeY/2; j--) {
        log_info("Point: " << pointCount << "(" << i * xSpacing << ", " << j * ySpacing << ")"); 
        calibrationGrid[pointCount][0] = i * xSpacing;
        calibrationGrid[pointCount][1] = j * ySpacing;
        pointCount++;
      }
    }
  }
}


void Maslow_::reset_all_axis(){
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}

//Checks to see how close we are to the targetX and targetY position. If we are within the tolerance, we are on target and return true;
bool Maslow_::onTarget(double targetX, double targetY, double currentX, double currentY, double tolerance) {
  if (abs(targetX - currentX) < tolerance && abs(targetY - currentY) < tolerance) {
    return true;
  }
  return false;
}

// move pulling just two belts depending on the direction of the movement
bool Maslow_::move_with_slack(double fromX, double fromY, double toX, double toY) {
  
  //This is where we want to introduce some slack so the system
  static unsigned long decompressTimer    = millis();
  static bool          decompress         = true;

//We conly want to decompress at the beginning of each move
if(decompress){
    decompressTimer = millis();
    decompress = false;
}

//Decompress belts for 500ms...this happens by returning right away before running any of the rest of the code
  if (millis() - decompressTimer < 500) {
      axisTL.recomputePID();
      axisTR.recomputePID();
      axisBL.decompressBelt();
      axisBR.decompressBelt();
      return false;
  }

  //Stop for 50ms
  //we need to stop motors after decompression was finished once
  else if (millis() - decompressTimer < 550) {
      stopMotors();
  }

  //How big of steps do we want to take with each loop?
  double stepSize = 0.01;

  double currentXTarget = getTargetX();
  double currentYTarget = getTargetY();

  //Compute the direction in X and Y
  int xDirection = currentXTarget - toX > 0 ? -1 : 1;
  int yDirection = currentYTarget - toY > 0 ? -1 : 1;

  setTargets(currentXTarget + xDirection * stepSize, currentYTarget + yDirection * stepSize, 0, true, true, false, false);

  int direction  = get_direction(fromX, fromY, toX, toY);
  int comply_spd = 1500;
  
  switch (direction) {
    case UP:
        axisTL.recomputePID();
        axisTR.recomputePID();
        axisBL.comply(comply_spd);
        axisBR.comply(comply_spd);
        if( onTarget(toX, toY, getTargetX(), getTargetY(), 0.5) ) {
            stopMotors();
            reset_all_axis();
            decompress = true; //Reset for the next pass
            return true;
        }
        break;
      case DOWN:
            axisTL.comply(comply_spd);
            axisTR.comply(comply_spd);
            axisBL.recomputePID();
            axisBR.recomputePID();
            if( axisBL.onTarget(0.5) && axisBR.onTarget(0.5) ) {
                stopMotors();
                reset_all_axis();
                decompress = true; //Reset for the next pass
                return true;
            }
            break;
      case LEFT:
            axisTL.recomputePID();
            axisTR.comply(comply_spd);
            axisBL.recomputePID();
            axisBR.comply(comply_spd);
            if( axisTL.onTarget(0.5) && axisBL.onTarget(0.5) ) {
                stopMotors();
                reset_all_axis();
                decompress = true; //Reset for the next pass
                return true;
            }
            break;
      case RIGHT:
            axisTL.comply(comply_spd);
            axisTR.recomputePID();
            axisBL.comply(comply_spd);
            axisBR.recomputePID();
            if( axisTR.onTarget(0.5) && axisBR.onTarget(0.5) ) {
                stopMotors();
                reset_all_axis();
                decompress = true; //Reset for the next pass
                return true;                
            }
            break;
  }
  return false;
}

//direction from maslow current coordinates to the target coordinates
int Maslow_::get_direction(double x, double y, double targetX, double targetY){
    
    int direction = 0;

    if(orientation == VERTICAL) return UP;

    if( targetX-x > 1) {
        direction = RIGHT;
    }
    else if( targetX-x < -1) {
        direction = LEFT;
    }
    else if( targetY-y > 1) {
        direction = UP;
    }
    else if( targetY-y < -1) {
        direction = DOWN;
    }

    return direction;
} 

//This is the function that should prevent machine from damaging itself
void Maslow_::safety_control() {

  //We need to keep track of average belt speeds and motor currents for every axis
    static bool tick[4] = {false, false, false, false};
    static unsigned long spamTimer = millis();

  MotorUnit* axis[4] = { &axisTL, &axisTR, &axisBL, &axisBR };
  for (int i = 0; i < 4; i++) {
      //If the current exceeds some absolute value, we need to call panic() and stop the machine
      if (axis[i]->getMotorCurrent() > currentThreshold+220000  && !tick[i]) {
          log_error("Motor current on " << axis_id_to_label(i).c_str() << " axis exceeded threshold of " << currentThreshold+220000
                                        << "mA, current is " << int(axis[i]->getMotorCurrent()) << "mA");
          Maslow.panic();
          tick[i] = true;
      }

      //If the motor torque is high, but the belt is not moving
      //  if motor is moving IN, this means the axis is STALL, we should warn the user and lower torque to the motor
      //  if the motor is moving OUT, that means the axis has SLACK, so we should warn the user and stop the motor, until the belt starts moving again
      // don't spam log, no more than once every 5 seconds
      
      static int axisStallCounter[4] = {0,0,0,0};
      static int axisSlackCounter[4] = {0,0,0,0};

      if (axis[i]->getMotorCurrent() > currentThreshold && abs (axis[i]->getBeltSpeed() ) < 0.1 && !tick[i] ) {
            axisStallCounter[i]++;
            if(axisStallCounter[i] > 2){
                //log_info("STALL:" << axis_id_to_label(i).c_str() << " motor current is " << int(axis[i]->getMotorCurrent()) << "mA, but the belt is not moving");
                tick[i] = true;
                axisStallCounter[i] = 0;
            }
        }
        else axisStallCounter[i] = 0;

      if(axis[i]->getMotorPower() > 750 && abs (axis[i]->getBeltSpeed() ) < 0.1 && !tick[i]){
            axisSlackCounter[i]++;
            if(axisSlackCounter[i] > 20){
                //log_info("SLACK:" << axis_id_to_label(i).c_str() << " motor power is " << int(axis[i]->getMotorPower()) << "mW, but the belt is not moving");
                tick[i] = true;
                axisSlackCounter[i] = 0;
                //Maslow.panic();
            }
      }
        else axisSlackCounter[i] = 0;
      
}

if(millis() - spamTimer > 5000){
    for(int i = 0; i < 4; i++){
        tick[i] = false;
    }
    spamTimer = millis();
}
}
// Maslow main loop
void Maslow_::update(){
    //Make sure we're running maslow config file
    if(!Maslow.using_default_config){

        Maslow.updateEncoderPositions(); //We always update encoder positions in any state, belt speeds are updated there too
        //update motor currents and belt speeds like this for now
        axisTL.update();
        axisTR.update();
        axisBL.update();
        axisBR.update();
        if(safetyOn) safety_control();

        //quick solution for delay without blocking
        if(holding && millis() - holdTimer > holdTime){
            holding = false;
        }
        else if (holding) return;

        if(test){
            if(take_measurement_avg_with_check(0)) test = false;
        }

        //Maslow State Machine

        //Jog or G-code execution. Maybe need to add more modes here like Hold? 
        if( sys.state() == State::Jog || sys.state() == State::Cycle  ){

            Maslow.setTargets(steps_to_mpos(get_axis_motor_steps(0),0), steps_to_mpos(get_axis_motor_steps(1),1), steps_to_mpos(get_axis_motor_steps(2),2));
            Maslow.recomputePID();
        }
        
        //Homing routines
        else if(sys.state() == State::Homing){

            home();

        }
        else { //In any other state, keep motors off
        
        
        digitalWrite(coolingFanPin, HIGH);
        //We need to fix the cooling fan turning on at the right times. I think that we want it on any time any motor is moving
        // if(sys.state() != State::Idle){
        //     digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on
        // }
        // else {
        //     digitalWrite(coolingFanPin, LOW);  //Turn the cooling fan off
        // }

        if(!test) Maslow.stopMotors();
 
        }

        //if the update function is not being called enough, stop everything to prevent damage
        if(millis() - lastCallToUpdate > 500){
            Maslow.panic();
            // print warnign and time since last call
            int elapsedTime = millis()-lastCallToUpdate; 
            log_error("Emergency stop. Update function not being called enough."  << elapsedTime << "ms since last call" );
        }

    }
    lastCallToUpdate = millis();
}

void Maslow_::test_(){
            //     generate_calibration_grid();
            // for(int i = 0; i < CALIBRATION_GRID_SIZE; i++){
            //     log_info("x: " << calibrationGrid[i][0] << " y: " << calibrationGrid[i][1]);
            // }
            axisTL.setTarget( axisTL.getPosition() ) ;
            axisTR.setTarget( axisTR.getPosition() ) ;
            axisBL.setTarget( axisBL.getPosition() ) ;
            axisBR.setTarget( axisBR.getPosition() ) ;
            x = 0;
            y = 0;
    test = true;
}

//non-blocking homing functions
void Maslow_::retractTL(){
    //We allow other bells retracting to continue 
    retractingTL = true;
    complyALL = false;
    extendingALL = false;
    axisTL.reset();
    log_info("Retracting Top Left");
}
void Maslow_::retractTR(){
    retractingTR = true;
    complyALL = false;
    extendingALL = false;
    axisTR.reset();
    log_info("Retracting Top Right");
}
void Maslow_::retractBL(){
    retractingBL = true;
    complyALL = false;
    extendingALL = false;
    axisBL.reset();
    log_info("Retracting Bottom Left");
}
void Maslow_::retractBR(){
    retractingBR = true;
    complyALL = false;
    extendingALL = false;
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
    // ADD also shouldn't extend before we get the parameters from the user

    if(!all_axis_homed()){
        log_error("Cannot extend all until all axis are homed"); //I keep getting everything set up for calibration and then this trips me up
        sys.set_state(State::Idle);
        return;
    }

    stop();
    extendingALL = true;
    log_info("Extending All");
}

void Maslow_::runCalibration(){

    stop();
    //if not all axis are homed, we can't run calibration, OR if the user hasnt entered width and height? 
    if(!all_axis_homed()){
        log_error("Cannot run calibration until all axis are homed");
        sys.set_state(State::Idle);
        return;
    }

    if(frame_width < frame_dimention_MIN || frame_width > frame_dimention_MAX || frame_height < frame_dimention_MIN || frame_height > frame_dimention_MAX){
        log_error("Cannot run calibration until frame width and height are set");
        sys.set_state(State::Idle);
        return;
    }
    sys.set_state(State::Homing);
    //generate calibration map 
    generate_calibration_grid();
    calibrationInProgress = true;
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
void Maslow_::setSafety(bool state){
    safetyOn = state;
}
//updating encoder positions for all 4 arms, cycling through them each call, at ENCODER_READ_FREQUENCY_HZ frequency
bool Maslow_::updateEncoderPositions(){
    bool success = true;
    static unsigned long lastCallToEncoderRead = millis();

    static int encoderFailCounter[4] = {0,0,0,0}; 
    static unsigned long encoderFailTimer = millis();

    if(!readingFromSD  && (millis() - lastCallToEncoderRead > 1000/(ENCODER_READ_FREQUENCY_HZ) ) ) {
        static int encoderToRead = 0;
        switch(encoderToRead){
            case 0:
                if(!axisTL.updateEncoderPosition()){
                    encoderFailCounter[0]++;
                }
                break;
            case 1:
                if(!axisTR.updateEncoderPosition()){
                    encoderFailCounter[1]++;
                }
                break;
            case 2:
                if(!axisBL.updateEncoderPosition()){
                    encoderFailCounter[2]++;
                }
                break;
            case 3:
                if(!axisBR.updateEncoderPosition()){
                    encoderFailCounter[3]++;
                }
                break;
        }
        encoderToRead++;
        if(encoderToRead > 3) {
            encoderToRead = 0;
            lastCallToEncoderRead = millis();
        }
    }

    // if more than 10% of readings fail, warn user, if more than 50% fail, stop the machine and raise alarm
    if(millis() - encoderFailTimer > 1000){
        for(int i = 0; i < 4; i++){
            //turn i into proper label
            String label = axis_id_to_label(i);
            if(encoderFailCounter[i] > 0.5*ENCODER_READ_FREQUENCY_HZ){
                // log error statement with appropriate label
                log_error("Failure on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i] << " times in the last second");
                Maslow.panic();
            }
            else if(encoderFailCounter[i] > 0.1*ENCODER_READ_FREQUENCY_HZ){
                log_info("Bad connection on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i] << " times in the last second");
            }
            encoderFailCounter[i] = 0;
            encoderFailTimer = millis();
        }
    }
    //DEBUG: keep track of encoder positions and print encoder positions if they changed since the last call:
    // static float lastEncoderPositions[4] = {0};
    // float encoderPositions[4] = {0};
    // encoderPositions[0] = axisTL.getPosition();
    // encoderPositions[1] = axisTR.getPosition();
    // encoderPositions[2] = axisBL.getPosition();
    // encoderPositions[3] = axisBR.getPosition();
    // bool encoderPositionsChanged = false;
    // for(int i = 0; i < 4; i++){
    //     if(abs( encoderPositions[i] - lastEncoderPositions[i]) > 1){
    //         encoderPositionsChanged = true;
    //         lastEncoderPositions[i] = encoderPositions[i];
    //     }
    // }
    // if(encoderPositionsChanged){
    //     log_info("Encoder Positions: " << encoderPositions[0] << " " << encoderPositions[1] << " " << encoderPositions[2] << " " << encoderPositions[3]);
    // }
    
    return success;
}

String Maslow_::axis_id_to_label(int axis_id){
    String label;
    switch(axis_id){
        case 0:
            label = "Top Left";
            break;
        case 1:
            label = "Top Right";
            break;
        case 2:
            label = "Bottom Left";
            break;
        case 3:
            label = "Bottom Right";
            break;
    }
    return label;
}

//Called from update()
void Maslow_::recomputePID(){
    //limit frequency to 500 Hz , maybe better update only if the encoder positions where updated
    // if(millis() - lastCallToPID < 2){
    //     return;
    // }
    // lastCallToPID = millis();
    // if(readingFromSD){
    //     return;
    // }

   
    axisBL.recomputePID();
    axisBR.recomputePID();
    axisTR.recomputePID();
    axisTL.recomputePID();
    digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on

    if (digitalRead(SERVOFAULT) == 1) { //The servo drives have a fault pin that goes high when there is a fault (ie one over heats). We should probably call panic here. Also this should probably be read in the main loop
        log_info("Servo fault!");
    }
}

// Stop all motors and reset all state variables
void Maslow_::stop(){
    stopMotors();
    retractingTL = false;
    retractingTR = false;
    retractingBL = false;
    retractingBR = false;
    extendingALL = false;
    complyALL = false;
    calibrationInProgress = false;
    test = false; 
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}

//Stop all the motors
void Maslow_::stopMotors(){
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
    //digitalWrite(coolingFanPin, LOW); //Turn off the cooling fan
}

void Maslow_::panic(){
    log_error("PANIC! Stopping all motors");
    stop();
    sys.set_state(State::Alarm);
}

void Maslow_::set_frame_width(double width){
    frame_width = width;
    update_frame_xyz();
    updateCenterXY();
}
void Maslow_::set_frame_height(double height){
    frame_height = height;
    update_frame_xyz();
    updateCenterXY();
}
// update coordinates of the corners based on the frame width and height
void Maslow_::update_frame_xyz(){
    blX = 0;
    blY = 0;
    blZ = 0;

    brY = 0;
    brX = frame_width;
    brZ = 0;

    tlX = 0;
    tlY = frame_height;
    tlZ = 0;

    trX = frame_width;
    trY = frame_height;
    trZ = 0;

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
    //float extraSlack = min(max(-34.48*trTension - 32.41, 0.0), 8.0); //limit of 0-2mm of extension

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

    //float extraSlack = min(max(-34.48*tlTension - 32.41, 0.0), 8.0); //limit of 0-2mm of extension

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

/*
* This computes the target lengths of the belts based on the target x and y coordinates
* and sends that information to each arm.
*/
void Maslow_::setTargets(float xTarget, float yTarget, float zTarget, bool tl, bool tr, bool bl, bool br){

        //Store the target x and y coordinates for the getTargetN() functions
        targetX = xTarget;
        targetY = yTarget;
        targetZ = zTarget;

        computeTensions(xTarget, yTarget);

        if(tl){
            axisTL.setTarget(computeTL(xTarget, yTarget, zTarget));
        }
        if(tr){
            axisTR.setTarget(computeTR(xTarget, yTarget, zTarget));
        }
        if(bl){
            axisBL.setTarget(computeBL(xTarget, yTarget, zTarget));
        }
        if(br){
            axisBR.setTarget(computeBR(xTarget, yTarget, zTarget));
        }
}

/*
* Get's the most recently set target position in X
*/
double Maslow_::getTargetX(){
    return targetX;
}

/*
* Get's the most recently set target position in Y
*/
double Maslow_::getTargetY(){
    return targetY;
}

/*
* Get's the most recently set target position in Z
*/
double Maslow_::getTargetZ(){
    return targetZ;
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////// OLD SHIT  /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////



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

//Runs the calibration sequence to determine the machine's dimensions // DONT RUN, DOESN'T WORK
void Maslow_::runCalibration_(){

    if(!all_axis_homed()){
        log_error("Cannot run calibration until all axis are homed");
        sys.set_state(State::Idle);
        return;
    }

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

