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

#include "SparkFun_I2C_Mux_Arduino_Library.h"

//Sets up the Mux for basic function
//Returns true if device responded correctly. All ports will be disabled.
bool QWIICMUX::begin(uint8_t deviceAddress, TwoWire &wirePort)
{
  //Get user's options
  _i2cPort = &wirePort;
  _deviceAddress = deviceAddress;

  //Valid addresses for the TCA9548 are 0x70 to 0x77.
  //We don't check the validity of the address. There may be other I2C mux's outside of this range.

  //Check if the device ack's over I2C
  if (isConnected() == false)
    return (false);
  return (true);
}

//Returns true if device is present
//Tests for device ack to I2C address
//Then tests if device behaves as we expect
//Leaves with all ports disabled
bool QWIICMUX::isConnected()
{
  _i2cPort->beginTransmission(_deviceAddress);
  if (_i2cPort->endTransmission() != 0)
    return (false); //Device did not ACK

  //Write to device, expect a return
  setPortState(0xA4); //Set port register to a known value
  uint8_t response = getPortState();
  setPortState(0x00);   //Disable all ports
  if (response == 0xA4) //Make sure we got back what we expected
    return (true);      //All good
  return (false);
}

//Enables one port. Disables all others.
//If port number if out of range, disable all ports
bool QWIICMUX::setPort(uint8_t portNumber)
{
  uint8_t portValue = 0;

  if (portNumber > 7)
    portValue = 0; //If port number is out of range, turn off all ports
  else
    portValue = 1 << portNumber;

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(portValue);
  if (_i2cPort->endTransmission() != 0)
    return (false); //Device did not ACK
  return (true);
}

//Returns the first port number bit that is set
//Returns 255 if no port is enabled
//Return 254 if there is an I2C error
uint8_t QWIICMUX::getPort()
{
  //Read the current mux settings
  //_i2cPort->beginTransmission(_deviceAddress); <- Don't do this!
  _i2cPort->requestFrom(_deviceAddress, uint8_t(1));
  if (!_i2cPort->available())
    return (254); //Error
  uint8_t portBits = _i2cPort->read();

  //Search for the first set bit, then return its location
  for (uint8_t x = 0; x < 8; x++)
  {
    if (portBits & (1 << x))
      return (x);
  }
  return (255); //Return no port set
}

//Writes a 8-bit value to mux
//Overwrites any other bits
//This allows us to enable/disable multiple ports at same time
bool QWIICMUX::setPortState(uint8_t portBits)
{
  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(portBits);
  if (_i2cPort->endTransmission() != 0)
    return (false); //Device did not ACK
  return (true);
}

//Gets the current port state
//Returns byte that may have multiple bits set
uint8_t QWIICMUX::getPortState()
{
  //Read the current mux settings
  //_i2cPort->beginTransmission(_deviceAddress); <- Don't do this!
  _i2cPort->requestFrom(_deviceAddress, uint8_t(1));
  return (_i2cPort->read());
}

//Enables a specific port number
//This allows for multiple ports to be 'turned on' at the same time. Use with caution.
bool QWIICMUX::enablePort(uint8_t portNumber)
{
  if (portNumber > 7)
    portNumber = 7; //Error check

  //Read the current mux settings
  uint8_t settings = getPortState();

  //Set the wanted bit to enable the port
  settings |= (1 << portNumber);

  return (setPortState(settings));
}

//Disables a specific port number
bool QWIICMUX::disablePort(uint8_t portNumber)
{
  if (portNumber > 7)
    portNumber = 7; //Error check

  uint8_t settings = getPortState();

  //Clear the wanted bit to disable the port
  settings &= ~(1 << portNumber);

  return (setPortState(settings));
}
