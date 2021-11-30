#pragma once

#include <cstdint>

// PWM:
uint32_t getApbFrequency();  // In Hz
double   ledcSetup(uint8_t channel, double freq, uint8_t resolution_bits);
void     ledcWrite(uint8_t channel, uint32_t duty);
void     ledcAttachPin(uint8_t pin, uint8_t channel);
void     ledcDetachPin(uint8_t pin);

void pinMatrixOutAttach(uint8_t pin, uint8_t function, bool invertOut, bool invertEnable);
void pinMatrixOutDetach(uint8_t pin, bool invertOut, bool invertEnable);
