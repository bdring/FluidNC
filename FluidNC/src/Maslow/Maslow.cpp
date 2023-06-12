#include "Maslow.h"
#include "../Report.h"

void Maslow_::begin() {
  initialized = 1;

  Serial.begin(115200);

  motorUnit1.begin(0);
  motorUnit2.begin(1);
  motorUnit3.begin(2);
  motorUnit4.begin(3);
}

void Maslow_::readEncoders() {
  Serial.println("Reading Encoders");
  motorUnit1.readEncoder();
  motorUnit2.readEncoder();
  motorUnit3.readEncoder();
  motorUnit4.readEncoder();
}

void Maslow_::home(int axis){
  log_info("Maslow home ran");
  log_info(initialized);
}

Maslow_ &Maslow_::getInstance() {
  static Maslow_ instance;
  return instance;
}

Maslow_ &Maslow = Maslow.getInstance();