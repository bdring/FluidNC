/* List of files and sections to place in RAM instead of FLASH */
/* See README.md in this directory for a complete explanation */

    *Laser.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)

    /* All files whose name ends with Spindle.cpp */
    **Spindle.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)

    /* Motors */
    *Motor.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *Dynamixel2.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *MotorDriver.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *NullMotor.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *RcServo.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *Servo.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *StandardStepper.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *StepStick.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *TrinamicBase.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *TrinamicSpiDriver.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *TrinamicUartDriver.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *UnipolarMotor.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)

    /* Pin Details */
    **Detail.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *LedcPin.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
    *Pin.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
