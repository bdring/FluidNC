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

#define DC_TOP_LEFT_MM_PER_REV 44
#define DC_Z_AXIS_MM_PER_REV 1

void Maslow_::begin() {
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

  tlX = -8.339;
  tlY = 2209;
  tlZ = 172;
  trX = 3505; 
  trY = 2209;
  trZ = 111;
  blX = 0;
  blY = 0;
  blZ = 96;
  brX = 3505;
  brY = 0;
  brZ = 131;

  tlTension = 0;
  trTension = 0;
  
  //Recompute the center XY
  updateCenterXY();
  
  _beltEndExtension = 30;
  _armLength = 114;

}

void Maslow_::readEncoders() {
  Serial.println("Reading Encoders");
  axisTL.readEncoder();
  axisTR.readEncoder();
  axisBL.readEncoder();
  axisBR.readEncoder();
}

void Maslow_::home(int axis){
  log_info("Maslow home ran");
  log_info(initialized);
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