#pragma once
#include <Arduino.h>
#include "MotorUnit.h"

class Maslow_ {
  private:
    Maslow_() = default; // Make constructor private

  public:
    static Maslow_ &getInstance(); // Accessor for singleton instance

    Maslow_(const Maslow_ &) = delete; // no copying
    Maslow_ &operator=(const Maslow_ &) = delete;

  public:
    void begin();
    void readEncoders();
    void home(int axis);
    MotorUnit motorUnit1;
    MotorUnit motorUnit2;
    MotorUnit motorUnit3;
    MotorUnit motorUnit4;
    int initialized = 0;
};

extern Maslow_ &Maslow;