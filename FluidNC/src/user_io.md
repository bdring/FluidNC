## User I/O Commands

This is for generic I/O control. These could be for cooling fans, clamping, valves, etc. These could be digital (on/off) or analog (PWM). This allows you to do gcode rather than special coding.

### Types

- Digital I/O
- Analog (PWM)

Commands

#### M62 - M65 Digital Output Control 

M62 Synchronized Digital Output On

M63 Synchronized Digital Output Off

M64 Immediate Digital Output On

M65 Immediate Digital Output Off

Use P word to indicate digital output number (0-3)

Examples:

M62 P0

M63 P0

#### M67 - M68 Digital Output Control 

M67 Synchronized Set Analog Value

M68 Immediate Set Analog Value

Use E word for analog output number (0-3)

Use Q word for analog value in percent duty.

Examples 

M67 E1 Q23.87 (set set output#1 to 23.87% duty)

M67 E1 Q0 (to turn off the pin. 0% duty)

### Pin Numbering

Each pin will be given a number starting at 0. The numbers for the digital and analog are independent. If you use one of each, they should both be 0.

### Synchronized

Synchronized means all steps in the buffers must complete before the I/O is changed. Immediate does not wait. With streaming gcode, you should use the synchronized commands. There is no timing guarantee with immediate versions of the commands.

## Special Behaviors

- Power On - All pins will be set to the off state
- Reset - All pins will be set to the off state
- Alarm Mode - No change in pin state
- Error - No change in pins state
- End of File - No change in pins state

### Setup

```
#define USER_DIGITAL_PIN_0   GPIO_NUM_xx
#define USER_DIGITAL_PIN_1   GPIO_NUM_xx
#define USER_DIGITAL_PIN_2   GPIO_NUM_xx
#define USER_DIGITAL_PIN_3   GPIO_NUM_xx

#define USER_ANALOG_PIN_0      GPIO_NUM_xx
#define USER_ANALOG_PIN_1      GPIO_NUM_xx
#define USER_ANALOG_PIN_2      GPIO_NUM_xx
#define USER_ANALOG_PIN_3      GPIO_NUM_xx

#define USER_ANALOG_PIN_1_FREQ      50  // Hz
```

### Frequency

Defining a frequency is optional. If you do not define one for a pin the default of 5000 will be used.

### Resolution

The resolution is dependent on the frequency used. The PWM is based on a 80MHz timer. If you have a 10KHz frequency, 80,000,000 / 10,000 gives you a maximum of an 8,000 count resolution. The resolution is based on bits so you need to round down to the nearest bit value which would be 12 bit or 4096. The highest bit value allowed is 13 bits.

This is all done behind the scenes, you only have to provide the frequency and duty.

### Duty

The duty is a percentage of the period. It you are looking for a specific pulse width, you need to determine the period, which is 1/freq. If your frequency is 100Hz  your period is 10ms. If you want a 1ms pulse, you would set the duty to 0.10%. Duty is a floating point number. A value of XX.XX will get you the highest resolution. Values above 100% are set at 100%. Negative numbers are set to 0%. 

### Use With RC Servos

RC Servos set their position based on the pulse length of a PWM signal. The standard frequency for RC servos PWM is 50Hz, some digital servos can handle a higher rate, but it will not improve performance in this application and could overheat some servos.

The standard pulse range for RC servos is 1ms to 2ms. Some servos have a wider range.

With a 50Hz frequency the period is 20ms. Therefore a 1ms pulse is 5% duty and a 2ms pulse is 10% duty. Use the 2 values to to go from one end of travel to the other. You can use values in between to go to other positions. You can experiment with a little less or more if you servos have a bigger range.

You can also set the duty to 0%. This generally turns a servo off and allows it to free wheel and not draw much power.