# Google test code

These unit tests are designed to test a small portion of the GRBL_ESP32
code, directly from your desktop PC. This is not a complete test of 
GRBL_ESP32, but a starting point from which we can move on. Testing and 
debugging on a desktop machine is obviously much more convenient than 
it is on an ESP32 with a multitude of different configurations, not to
mention the fact that you can use a large variety of tools such as 
code coverage, profiling, and so forth.

Code here is split into two parts:
1. A subset of the GRBL code is compiled. Over time, this will become more.
2. Unit tests are executed on this code.

## Prerequisites

Google test framework.

## Folders and how this works

Support libraries are implemented that sort-of mimick the Arduino API where
appropriate. This functionality might be extended in the future, and is by 
no means intended to be or a complete or even a "working" version; it's 
designed to be _testable_.

Generally speaking that means that most features are simply not available. 
Things like GPIO just put stuff in a buffer, things like pins can be logged
for analysis and so forth. 

The "Support" folder is the main thing that gives this mimicking ability,
so that the code in the FluidNC folder is able to compile. For example,
when including `<Arduino.h>`, in fact `Support/Arduino.h` is included.

The include folders that have to be passed to the x86/x64 compiler are:

- X86TestSupport
- ..\FluidNC

## Test code

Google tests can be found in the `Tests` folder.
