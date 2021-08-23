## Dynamixel2

This allows Dynamixel Servo motors to be used as an axis motor. This document was written assuming XL430-250T servos were used, but other servo types that use [Robotis Protocol 2](https://emanual.robotis.com/docs/en/dxl/protocol2/) can probably be used (not tested).

They can be used in place of any lettered axis, but only one per axis. These servos are limited to a single 360° of rotation. This is mapped to the MaxTravel of the axis. If you want map 300mm of travel to an X axis servo, you set $X/MaxTravel=300.

This sets a range in machine space. You are allowed to zero the axis anywhere in that travel. If you do a move that exceeds the travel, the servo will move to the end of of travel and stay there. The axis position will still be running, but the servo will stop. You can use $Limits/Soft=On to alarm if the range is exceeded.

You should set the the axis speed and acceleration to what the servo can handle. If those settings are too high, the motor will still move, but will lag behind where the firmware thinks it is.

## Machine Definition

```
#define DYNAMIXEL_TXD           GPIO_NUM_4
#define DYNAMIXEL_RXD           GPIO_NUM_13
#define DYNAMIXEL_RTS           GPIO_NUM_17

#define X_DYNAMIXEL_ID          1 // protocol ID
#define Y_DYNAMIXEL_ID          2 // protocol ID
#define Z_DYNAMIXEL_ID          3 // protocol ID

#define SERVO_TIMER_INTERVAL    75   // milliseconds (an override...not required)
#define DXL_COUNT_MIN           1024 // (an override...not required)
#define DXL_COUNT_MAX           3072 // (an override...not required)
```


You need to specify the TXD, RXD and RTS pins you want to use for the half duplex communications bus.

The `SERVO_TIMER_INTERVAL` sets the time in milliseconds between updates. At each interval 1 message per servo is sent. If you try to update too fast you will see errors reported to the USB/Serial port. 75ms seems like a good rate for 3 servos. Adjust per your count.

You assign servos to axes with a definition like `#define X_DYNAMIXEL_ID          1` The servos should be programmed with unique IDs using Dynamixel software.

You can limit the servo rotational range of travel using `dxl_count_min` and `dxl_count_max` settings The full range of a XT430-250T servo is 0-4095.

You can revers direction using the standard $Stepper/DirInvert command

## Homing

Dynamixel servos, don't typically need to be homed. This is because they can determine their location from their encoders and report it to the firmware. If you do home them they will travel as fast as they can to the end of travel in the direction controlled by $Homing/DirInvert= setting.

## Manual Moves

Whenever the motors are disabled, you can manually home the servos. Since they are closed loop and report current position, they will report back where they have been moved to and update the position. This is controlled by the $Stepper/IdleDelay setting. 

The resolution is 4096 units per 360°. This is likely to be a lot coarser than the internal representation. Therefore if you send a servo to 15. It may change a little after being update from the servo when disabled. The servo may report back 14.8 after being sent to 15.







