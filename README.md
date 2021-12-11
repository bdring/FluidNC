<img src="https://github.com/bdring/FluidNC/wiki/images/logos/FluidNC.svg" width="600">

## Introduction

**FluidNC** is the next generation of the Grbl_ESP32 CNC control firmware. It has a lot of improvements over Grbl_ESP32 as listed below.

## Using / Compiling

You **do not need to compile** FluidNC. Each [release of FluidNC](https://github.com/bdring/FluidNC/releases) has an installation script that will automatically install the firmware to an ESP32. All standard configuration can be done by uploading configuration files later. See [this wiki page](https://github.com/bdring/FluidNC/wiki/FluidNC-Compiling).

## Firmware Architecture

- Object-Oriented hierarchical design
- Hardware abstraction for machine features like spindles, motors, and stepper drivers
- Extensible - Adding new features is much easier for the firmware as well as gcode senders.

## Machine Definition Method

Grbl_ESP32 used C preprocessor machine definition files (myMachineDef.h) and config.h to define a machine. Any change required a recompile. The goal with FluidNC is that virtually everyone uses the same compiled firmware and configures it with a configuration file in a simplified YAML format. That file is dynamically loaded from the local FLASH on ESP32. It can be uploaded via the serial port or Wifi.

You can have multiple config files stored on the ESP32. The default is config.yaml, but you can change that with $Config/Filename=<myOtherConfig.yaml>

## Basic Grbl Compatibility

The intent is to maintain as much Grbl compatibility as possible. It is 100% compatible with the day to day operations of running GCode with a sender, so there is no change to the Grbl GCode send/response protocol, and all Grbl GCodes are supported.  The Grbl $ settings and commands that a sender might issue after each reset are similarly supported.  Grbl $ settings that are used only for initial machine setup, then never changed unless the machine is physically reconfigured, will not be supported.  Grbl's $number=number method for machine setup is simply too weak to support the range of machine configurations that FluidNC can handle.  (In fact, Grbl's $number machine setup method was too weak even for classic Grbl, which required editing C files and recompiling for many kinds of changes.) What this means is that existing senders will still be able to run GCode jobs on Fluid, but the "setup wizards" that a few senders have will not work in their current form.  We are working on a mechanism to enable even better setup wizards - graphical configuration helpers - that can handle the vast range of machines that FluidNC supports, extensible so that senders will be able to automatically support new FluidNC features without sender code changes.


## I/O Pins

Pin configuration is a key step in machine setup.  You must assign specific pins - be they GPIOs or pins on I/O expander chips - to functions like stepper control, limit switches, spindle control, and I/O buses that communicate with other devices.  Furthermore, pins have attributes like active state (high or low) and internal pullups that must be specified.  In classic Grbl, you assign pins by editing a ".h" C preprocessor file, then recompiling. Classic Grbl does let you choose the active state (but not the pullup states) for some pins with $ commands, but the interface is clumsy, requiring the user to perform binary-to-decimal math on "bitmask" variables.  The bitmask technique, in addition to being obscure to users who are not programmers, cannot handle things like multiple independent motors on the same axis.

FluidNC's pin assignment syntax is clear, straightforward, capable and consistent.  For example, to assign GPIO 4 as the probe pin, active low, with internal pullups enabled, you write:
```
probe:
  pin: gpio.4:low:pu
```

The following example illustrates that both input and output pins can be assigned in the same way, with the direction being implicit in the function that the pin is used for.

```
control:
   feed_hold: gpio.14:low:pu
coolant:
   mist: gpio.21
```

The feed hold pin, known to be an input because that is what the feed hold function requires, is set to GPIO 12, active low so a feed hold will be triggered when the pin goes to the electrical low state.  The internal pullup is enabled, which is appropriate and necessary if there is no external pullup resistor or active high drive on that signal.  The mist pin, known to be an output because the mist function controls an external relay or similar actuator, is assigned to GPIO 21.  The signal is active high, which is the default if neither ":low" nor ":high" is explicitly mentioned, and no internal pullups are enabled.  If GCode turns on mist coolant, the pin will go to the electrical high state.

The firmware knows additional requirements for pins based on how they are used, applying and checking those automatically.  For example, a limit pin must be an input that support interrupts, so the attempt to assign a hardware pin that cannot meet those requirements triggers an error message.  Other errors, such at an attempt to use the same pin for two different purposes, are similarly checked.

For functions that are not used, you can either omit the line that would assign a pin to that function, or explicitly list it as "no_pin".  For example, these two are equivalent, saying that flood coolant is not supported - and attempts to turn on flood coolant will be silently ignored:

```
coolant:
  flood: no_pin
```
or you can just omit the "flood:" line in the coolant section - or omit the entire coolant section if the machine does not support any form of coolant control.
```
coolant:
```

## Tuning

Many parameters like accelerations and speeds need to be tuned when setting up a new machine. These values are in the config file like everything else, but you can also temporarily change them by sending the config file hierarchy for that setting as a $ command. Sending $axes/x/acceleration=50 would change the acceleration of the x axis to 50. 

The changes are temporary. To make them permanent you would edit the config file. You can also save the all of the current changes to the config file with the $CD=<your config file name>

You can show the value of an individual parameter by sending its $ name with the =, as:
```
ok
$axes/x/acceleration
$axes/x/acceleration=25.000
ok
```
The first $ line above is what you send, the second is FluidNC's response.

You can show entire subsections by sending the section name:

```
ok
$coolant
coolant:
  flood: NO_PIN
  mist: NO_PIN
  delay_ms: 0
ok
```

## Spindles

FluidNC supports multiple spindles on one machine.  Spindles can be controlled by different hardware interfaces like relays, PWM, DACs, or RS485 serial interfaces to VFDs.  Lasers are treated as spindles. Each spindle is assigned a range of tool numbers. You change spindles by issuing the GCode command "M6 Tn", where n is the tool number.  Tool numbers within the assigned range for a given spindle will activate that spindle - and the detailed number within the range could be used to select the specific tool on the spindle.  This lets you have, for example, a single machine with an ATC spindle and a laser. A single GCode file could allow you to etch and cutout a part. Most CAM programs support tool numbers. You could also have a gantry with both a low-speed high-torque pulley spindle and also a high-speed direct drive spindle. 

## Motors

FluidNC supports, without recompiling, different motor types like

* Stepper motors
* Servo motors
* Dynamixel smart servos

## Stepper Drivers
FluidNC supports, without recompiling, different stepper motor drivers like

* "Dumb" (hardware configured) stepper drivers like A4988 and DRV8825
* External drivers
* Trinamic drivers configured either via hardware jumpers, SPI, or UART interfaces

## Homing and Limits

Homing and limit parameters are setup per axis. Each axis can have its own homing speeds, switch pull offs.  Soft limits can be configured, per axis, for arbitrary endpoints and distances.

FluidNC supports many different limit switch configurations.  You can have separate limit switches - on separate input pins - on each end of an axis (one on the positive end and the other on the negative end), or just one switch on a designated end, or a pair of switches on both ends that are wired together and fed into a single input pin.  The use of separate switches enables things like automatic pulloff for an axis that is already at a limit when the system starts.  On machines with dual (ganged) motors on one axis, that axis's limit switches can be shared (applying to both motors), or specific to each motor, thus allowing auto-squaring of the axis during homing.

## Motor Enables

Motor enable/disable pins can either be shared so that a single pin controls all motors, or per-motor so that you can separately enable specific motors.

## Ganged Motors

FluidNC supports up to two motors per axis, with independent drivers and homing.  In contrast to classic Grbl where "ganging" can only be done either by driving two motors from the same driver or by sending the same step and direction signal to two drivers, FluidNC's independent motors permit "auto-squaring" of axes. It also supports different switch pull offs in case your switches are not square.

## SD Card Support

## Multiple Communications Channels

FluidNC supports sender communication via multiple channels

* Serial / USB Serial Port (as with classic Grbl)
* Bluetooth serial
* Wifi/Telnet
* Wifi/WebSockets

## WebUI

FluidNC includes a built-in brower-based Web UI (Esp32_WebUI) so you control the machine from a PC, phone, or tablet on the same Wifi network.

### Credits

The original [Grbl](https://github.com/gnea/grbl) is an awesome project by Sungeon (Sonny) Jeon. I have known him for many years and he is always very helpful. I have used Grbl on many projects.

The Wifi and WebUI is based on [this project.](https://github.com/luc-github/ESP3D-WEBUI)  

Mitch Bradley designed and implemented the $name=value settings mechanism.  The config file format runtime configuration mechanism was spearheaded Mitch Bradley, and designed and implemented by Mitch and Stefan de Bruin.  Stefan's mastery of C++ is especially evident in the Class architecture of the configuration mechanism.

### Contribute

<img src="https://discord.com/assets/e05ead6e6ebc08df9291738d0aa6986d.png" width="8%"> There is a Discord server for the development this project. Ask for an invite

### FAQ

Start asking questions...I'll put the frequent ones here.



### Donation

This project requires a lot of work and often expensive items for testing. Please consider a safe, secure and highly appreciated donation via the PayPal link below or via the Github sponsor link at the top of the page.

[![](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=TKNJ9Z775VXB2)
