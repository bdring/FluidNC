# Vtable Linker Fixups for FluidNC

## The Problem

FluidNC uses C++ abstract classes with virtual methods for several
things including Spindles and Motors.  Unfortunately, virtual methods
do not work well within ESP32 interrupt service routines (ISRs).  ISR
code cannot execute reliably from FLASH due to intricate details of
the way that FLASH-resident code and data is cached.  You can specify
that certain code routines are to be placed in RAM instead of FLASH,
but that does not work for virtual methods because they are dispatched
through a data structure called a "vtable", and the ESP32 compilation
toolchain ordinarily puts vtables in FLASH.  So if you try to call a virtual
method from an ISR, you run the risk of crashes due to FLASH cache
management.

It is easy to force such a crash.  Just do a long GCode move and
try to load WebUI while the move is running.  Loading WebUI causes
a lot of FLASH filesystem accesses, quickly triggering a conflict
with a vtable FLASH access.

## The Solution

One solution would be to stop calling virtual methods from ISR code,
but that would require some tedious recoding.  Instead, we choose,
for now, to force the compilation toolchain to place vtables in RAM.

Changing the vtable placment requires modifications to the linker
scripts (.ld files) that control where things are placed in memory.
The linker scripts reside in the Arduino framework code where they
cannot easily be changed, but we can take advantage of some PlatformIO
capabilities to let us use a modified script.

### How It Normally Works

PlatformIO specifies the list of linker scripts via the Python script
~/.platformio/packages/framework-arduinoespressif32/tools/platformio-build.py .
The files are esp32.project.ld, esp32.rom.ld, esp32.peripherals.ld,
esp32.rom.libgcc.ld, and esp32.rom.spiram_incompatible_fns.ld .
The file that we need to change is esp32.project.ld ; the others can
be used as-is.

The stock linker scripts are found in the directory
.platformio/packages/framework-arduinoespressif32/tools/sdk/ld .  That
directory is specified in platformio-build.py via the environment
variable LIBPATH .

### Changes to esp32.project.ld

First, we need to modify esp32.project.ld .  We copy that file into
the FluidNC tree at ./FluidNC/ld/esp32/ (this directory).  We then
add into that file the line

   INCLUDE ( vtable_in_dram.ld )

The INCLUDE line is added inside the section that puts things into
data RAM.  It causes the contents of the new file "vtable_in_dram.ld"
to be processed in that context, which has the effect of forcing the
vtables from several code modules to be placed in RAM.  We use the
INCLUDE technique to isolate the changes into one new file, for easier
maintenance.

### Making PlatformIO Use the Modified File

To make PlatformIO use the modified esp32.project.ld instead
of the stock one, we use PlatformIO's "advanced scripting"
feature.  In the platformio.ini [env] section, we add this line

   extra_scripts = FluidNC/ld/esp32/vtable_in_dram.py

"vtable_in_dram.py" contains code that puts this directory at
the beginning of LIBPATH so this directory will be searched before
the standard list of directories.  Thus the linker will find
the modified esp32.project.ld instead of the stock version.

### Contents of vtable_in_dram.ld

The section types that must go into RAM are ".rodata" and ".xt.prop".
vtable_in_dram.ld contains lines like

```
   **Spindle.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
   *Dynamixel2.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
   *StepStick.cpp.o(.rodata .rodata.* .xt.prop .xt.prop.*)
```

The "*Spindle" line affects all files whose names end with "Spindle.cpp",
specifying that their .rodata and .xt.prop sections are to be placed
in RAM (because these lines are included from a dram section in the
enclosing .ld file).  The "Dynamixel2" and "StepStick" similarly affect
individual files.  We must list all files that are to be affected,
either individually or via a pattern.

### Implications and Possible Improvements

This change increases RAM usage by about 18K.  It might be possible
to reduce that by segregating the ISR code into separate files from
the rest of the class code that does not nave to be in RAM, so that
some of the .rodata could stay in FLASH.

If new motor types and new spindles are added, their file names
will need to be added to the list in vtable_in_dram.ld .  Alternatively,
we could create a naming convention for files that need this
special treatment, so a single pattern could catch all of them.

### Things that Do Not Work

PlatformIO has a board_build.ldscript feature.  I tried to use it
but got conflicts between the ldscript that it specified vs the
stock list of platform ld scripts in the Arduino framework.

You can pass extra linker options in build_flags by adding a line
like

 `-Wl,-TFluidNC/ld/esp32/my_script.ld`

That does not work because the additional script is processed
after all of the stock scripts, and conflicts with them.  To
accomplish the task at hand, it is necessary to get inside of
one of the stock scripts, hence the "copy/modify/substitute"
technique above.
