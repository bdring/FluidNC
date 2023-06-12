#include "MotorUnit.h"
#include "../Report.h"

#define TCAADDR 0x70

void MotorUnit::begin(int forwardPin,
               int backwardPin,
               int readbackPin,
               int encoderAddress,
               int channel1,
               int channel2){
    Serial.println("Beginning motor unit");

    _encoderAddress = encoderAddress;

    Wire.begin(5,4);

    encoder.begin();
}

void tcaselect(uint8_t i) {
  if (i > 7) return;
 
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}

void MotorUnit::readEncoder(){
    Serial.print("Encoder at address ");
    Serial.print(_encoderAddress);

    tcaselect(_encoderAddress);

    if(encoder.isConnected()){
        Serial.println(" is connected");
        log_info("Connected");
        log_info(_encoderAddress);
    } else {
        Serial.println(" is not connected");
    }
}

void MotorUnit::zero(){
    tcaselect(_encoderAddress);
    encoder.resetCumulativePosition();
}