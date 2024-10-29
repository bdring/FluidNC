/* List of files and sections to place in RAM instead of FLASH */
/* See README.md in this directory for a complete explanation */

    **Detail.cpp.o(.rodata .rodata.*)
/*    *Pin.cpp.o(.rodata .rodata.*) */

    /* All files whose name ends with Spindle.cpp */

    /* An earlier version included .xt.prop and .xt.prop.* but */
    /* it turns out that those bits can stay in FLASH */
    **Spindle.cpp.o(.rodata .rodata.*)
