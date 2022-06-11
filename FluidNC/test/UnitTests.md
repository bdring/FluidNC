# Unit tests

## Getting started

Getting started with unit tests:

1. Install G++/GCC. For Windows, I recommend MSYS2 (https://www.msys2.org/). 
   GCC is not installed for native targets by platformio, so you have to do 
   this. Make sure G++ is in the search path. This has been tested on gcc 
   version 11.2.0, which is the default for GCC at the time of writing.
2. Test G++. `g++ -v` from a command prompt should do the trick
3. Run all unit tests. 

Running unit tests is done by:

`pio test -e native`

## Making a unit test

Normally, if you make a new piece of code, you want to know that it's correct
and you want to ensure it *stays* correct. The latter is the main reason 
for unit tests; otherwise we should test the same features over and over 
again in the face of constant changes.

Making a test works as follows:

1. Create a new CPP file in the `test` folder, probably in some sub-folder
2. The contents should be something like this:

```c++
#include "../TestFramework.h"

#include <src/SomethingYouWantTested.h>

namespace Configuration {
    Test(TestCollectionName, TestName) {
        // doSomething...
        Assert(myCondition, "Description; test fails if !condition");

        // and do this for all functionalities exposed
    }
}

```

It's that easy. Once done, it should work with native unit tests.

# Google test code and unity tests

Google tests are the go-to tests for cross-platform testing, while 
Unity tests are the default on ESP32. Both have their advantages; for
example, Google Tests have great support for IDE integration.

We made the test framework in such a way that it switches the test 
framework being used depending on the platform. So, ESP32 run natively
on Unity, and the rest runs on GTest. 

These unit tests are designed to test a portion of the FluidNC
code, directly from your desktop PC. This is not a complete test of 
FluidNC, but a starting point from which we can move on. Testing and 
debugging on a desktop machine is obviously much more convenient than 
it is on an ESP32 with a multitude of different configurations, not to
mention the fact that you can use a large variety of tools such as 
code coverage, profiling, and so forth.

Code here is split into two parts:
1. A subset of the GRBL code is compiled. Over time, this will become more.
2. Unit tests are executed on this code.

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
when including `<Arduino.h>`, in fact `X86TestSupport/TestSupport/Arduino.h` is included.

The include folders that have to be passed to the x86/x64 compiler are:

- X86TestSupport
- ..\FluidNC

Unfortunately, not everything can be tested properly in an easy way. For 
example, it is very hard to test ESP32 DMA and low-level things. External 
libraries are also very nasty to test. In some cases we want to exclude 
portions of the source code. This can be done in PlatformIO.INI, in the
`src_filter` property of `[env:native]`. Take note: if you add things 
in the env section in PIO, it *must* be supported on both esp32 and native.

## Test code

Unit tests can be found in the `Tests` folder.

# Compiler details

Unit tests are currently running on 3 different platforms:

- Native x86 GCC/G++ through PlatformIO
- ESP32 GCC/G++ through PlatformIO
- MSVC++ through Visual Studio

## What's with -std=c++17

Well, apparently some of the features that the ESP32 compiler supports are 
actually C++17. In other words, you need a compiler capable of this syntax,
for the code to work.

All modern C++ Compilers support C++17. If yours doesn't, it's time for a
serious upgrade anyways, because it's definitely EOL.
