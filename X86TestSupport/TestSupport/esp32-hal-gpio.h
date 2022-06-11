#pragma once

#include <cstdint>

// Interrupt Modes
#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
// #define ONLOW     0x04 --> Weird. Same as falling / rising?
// #define ONHIGH    0x05
// #define ONLOW_WE  0x0C --> not sure about these
// #define ONHIGH_WE 0x0D

typedef enum {
    GPIO_NUM_0 = 0,   /*!< GPIO0, input and output */
    GPIO_NUM_1 = 1,   /*!< GPIO1, input and output */
    GPIO_NUM_2 = 2,   /*!< GPIO2, input and output
                             @note There are more enumerations like that
                             up to GPIO39, excluding GPIO20, GPIO24 and GPIO28..31.
                             They are not shown here to reduce redundant information.
                             @note GPIO34..39 are input mode only. */
                      /** @cond */
    GPIO_NUM_3  = 3,  /*!< GPIO3, input and output */
    GPIO_NUM_4  = 4,  /*!< GPIO4, input and output */
    GPIO_NUM_5  = 5,  /*!< GPIO5, input and output */
    GPIO_NUM_6  = 6,  /*!< GPIO6, input and output */
    GPIO_NUM_7  = 7,  /*!< GPIO7, input and output */
    GPIO_NUM_8  = 8,  /*!< GPIO8, input and output */
    GPIO_NUM_9  = 9,  /*!< GPIO9, input and output */
    GPIO_NUM_10 = 10, /*!< GPIO10, input and output */
    GPIO_NUM_11 = 11, /*!< GPIO11, input and output */
    GPIO_NUM_12 = 12, /*!< GPIO12, input and output */
    GPIO_NUM_13 = 13, /*!< GPIO13, input and output */
    GPIO_NUM_14 = 14, /*!< GPIO14, input and output */
    GPIO_NUM_15 = 15, /*!< GPIO15, input and output */
    GPIO_NUM_16 = 16, /*!< GPIO16, input and output */
    GPIO_NUM_17 = 17, /*!< GPIO17, input and output */
    GPIO_NUM_18 = 18, /*!< GPIO18, input and output */
    GPIO_NUM_19 = 19, /*!< GPIO19, input and output */

    GPIO_NUM_21 = 21, /*!< GPIO21, input and output */
    GPIO_NUM_22 = 22, /*!< GPIO22, input and output */
    GPIO_NUM_23 = 23, /*!< GPIO23, input and output */

    GPIO_NUM_25 = 25, /*!< GPIO25, input and output */
    GPIO_NUM_26 = 26, /*!< GPIO26, input and output */
    GPIO_NUM_27 = 27, /*!< GPIO27, input and output */

    GPIO_NUM_32  = 32, /*!< GPIO32, input and output */
    GPIO_NUM_33  = 33, /*!< GPIO33, input and output */
    GPIO_NUM_34  = 34, /*!< GPIO34, input mode only */
    GPIO_NUM_35  = 35, /*!< GPIO35, input mode only */
    GPIO_NUM_36  = 36, /*!< GPIO36, input mode only */
    GPIO_NUM_37  = 37, /*!< GPIO37, input mode only */
    GPIO_NUM_38  = 38, /*!< GPIO38, input mode only */
    GPIO_NUM_39  = 39, /*!< GPIO39, input mode only */
    GPIO_NUM_MAX = 40,
    /** @endcond */
} gpio_num_t;

// From esp32-hal-gpio.h:

#define LOW 0x0
#define HIGH 0x1

// GPIO FUNCTIONS
#define INPUT 0x01
#define OUTPUT 0x02
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
// #define OPEN_DRAIN        0x10
// #define OUTPUT_OPEN_DRAIN 0x12

void attachInterrupt(uint8_t pin, void (*)(void), int mode);
void attachInterruptArg(uint8_t pin, void (*)(void*), void* arg, int mode);
void detachInterrupt(uint8_t pin);

extern "C" int  __digitalRead(uint8_t pin);
extern "C" void __pinMode(uint8_t pin, uint8_t mode);
extern "C" void __digitalWrite(uint8_t pin, uint8_t val);
