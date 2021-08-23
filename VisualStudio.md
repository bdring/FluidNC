# Getting started with GRBL_ESP32 in Visual Studio

**!! Important !! There's a huge difference between Visual Studio and
Visual Studio Code. This document is about Visual Studio, not Visual
Studio Code!**

First, get PlatformIO to work with Visual studio. The steps that
need to be taken for this are the following:

1. Install python. This is needed for both PlatformIO and for generating
   the vcxproj file.
2. From https://docs.platformio.org/en/latest/core/index.html#piocore
   you should install the PlatformIO Core (CLI). Make sure you update
   the command line search path.
3. Use python to generate a vcxproj file: `python generate_vcxproj.py`.
4. Start FluidNC.sln

## Building

Building is as easy as building your solution.

## Uploading

Uploading can be done from the command line using platformio. For
example, run `platformio run --target upload --upload-port COM7`.
For more details, see [the documentation of pio](https://dokk.org/documentation/platformio/v3.6.1/platforms/espressif32/).
