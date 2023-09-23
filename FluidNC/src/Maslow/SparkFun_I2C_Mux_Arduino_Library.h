/*
  This is an Arduino library written for the TCA9548A/PCA9548A 8-bit multiplexer.
  By Nathan Seidle @ SparkFun Electronics, May 16th, 2020

  The TCA9548A/PCA9548A allows for up to 8 devices to be attached to a single
  I2C bus. This is helpful for I2C devices that have a single I2C address.

  https://github.com/sparkfun/SparkFun_I2C_Mux_Arduino_Library

  SparkFun labored with love to create this code. Feel like supporting open
  source? Buy a board from SparkFun!
  https://www.sparkfun.com/products/14685
*/

#ifndef SparkFun_I2C_Mux_Arduino_Library_h
#define SparkFun_I2C_Mux_Arduino_Library_h

#include "Arduino.h"
#include <Wire.h>

#define QWIIC_MUX_DEFAULT_ADDRESS 0x70

class QWIICMUX
{
public:
  bool begin(uint8_t deviceAddress = QWIIC_MUX_DEFAULT_ADDRESS, TwoWire &wirePort = Wire); //Check communication and initialize device
  bool isConnected();                                                                      //Returns true if device acks at the I2C address
  bool setPort(uint8_t portNumber);                                                        //Enable a single port. All other ports disabled.
  bool setPortState(uint8_t portBits);                                                     //Overwrite port register with all 8 bits. Allows multiple bit writing in one call.
  uint8_t getPort();                                                                       //Returns the bit position of the first enabled port. Useful for IDing which port number is enabled.
  uint8_t getPortState();                                                                  //Returns current 8-bit wide state. May have multiple bits set in 8-bit field.
  bool enablePort(uint8_t portNumber);                                                     //Enable a single port without affecting other bits
  bool disablePort(uint8_t portNumber);                                                    //Disable a single port without affecting other bits

private:
  TwoWire *_i2cPort;                                  //This stores the user's requested i2c port
  uint8_t _deviceAddress = QWIIC_MUX_DEFAULT_ADDRESS; //Default unshifted 7-bit address
};
#endif
