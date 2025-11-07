# FluidNC Repository Overview

## Table of Contents
1. [Introduction](#introduction)
2. [Project Purpose](#project-purpose)
3. [Repository Structure](#repository-structure)
4. [Architecture Overview](#architecture-overview)
5. [Core Components](#core-components)
6. [Build System](#build-system)
7. [Configuration System](#configuration-system)
8. [Operational States and Control](#operational-states-and-control)
9. [Development Guidelines](#development-guidelines)
10. [Installation and Usage](#installation-and-usage)
11. [Contributing](#contributing)
12. [Key Files and Directories](#key-files-and-directories)
13. [Resources](#resources)

---

## Introduction

**FluidNC** is a CNC firmware optimized for the ESP32 microcontroller. It is the next generation of firmware from the creators of Grbl_ESP32, designed to control a wide variety of CNC machine types with flexibility and modern features.

### Key Highlights
- **Target Platform**: ESP32 (standard and S3 variants)
- **License**: GNU General Public License v3.0 (GPLv3)
- **Language**: C++ (C++17 standard)
- **Build System**: PlatformIO
- **Configuration**: YAML-based configuration files (no recompilation needed)
- **Web Interface**: Built-in browser-based Web UI with WiFi/Bluetooth support

---

## Project Purpose

FluidNC provides a modern, flexible CNC firmware solution that:

1. **Eliminates Compilation Requirements**: Users configure machines via YAML files rather than recompiling firmware
2. **Hardware Abstraction**: Object-oriented design abstracts machine features (spindles, motors, stepper drivers)
3. **Multi-Tool Support**: Can control machines with multiple tool types (laser + spindle, tool changers)
4. **Grbl Compatibility**: Maintains compatibility with standard Grbl gcode and sender protocols
5. **Modern Connectivity**: Includes WiFi, Bluetooth, and WebSocket support for remote control
6. **Extensibility**: Adding new features is easier than traditional firmware architectures

---

## Repository Structure

```
FluidNC/
├── FluidNC/                    # Main source code directory
│   ├── src/                    # Core firmware source files
│   │   ├── Configuration/      # Configuration system (YAML parser, handlers)
│   │   ├── Kinematics/         # Motion systems (Cartesian, CoreXY, Delta, etc.)
│   │   ├── Machine/            # Machine components (axes, motors, homing)
│   │   ├── Motors/             # Motor driver implementations
│   │   ├── Pins/               # Pin management and mapping
│   │   ├── Spindles/           # Spindle control implementations
│   │   │   └── VFD/            # VFD (Variable Frequency Drive) protocols
│   │   ├── ToolChangers/       # Tool changer implementations
│   │   ├── WebUI/              # Web interface backend code
│   │   └── *.cpp, *.h          # Core system files (GCode, motion control, etc.)
│   ├── esp32/                  # ESP32-specific platform code
│   ├── stdfs/                  # Standard filesystem abstractions
│   ├── ld/                     # Linker scripts
│   ├── include/                # Header files
│   ├── data/                   # Data files (web UI assets, default configs)
│   └── tests/                  # Unit tests
├── libraries/                  # Custom libraries (ESP32SSDP)
├── example_configs/            # Example machine configuration files
├── install_scripts/            # Installation scripts (Windows, POSIX)
│   ├── win64/                  # Windows installation scripts
│   ├── posix/                  # Linux/Mac installation scripts
│   └── common/                 # Common installation resources
├── fluidterm/                  # Terminal application for interacting with FluidNC
├── fixture_tests/              # Integration/fixture tests
├── X86TestSupport/             # Support files for x86 testing
├── .github/                    # GitHub workflows and issue templates
├── platformio.ini              # PlatformIO build configuration
├── README.md                   # Project README
├── LICENSE                     # GPLv3 license text
├── CodingStyle.md              # Coding standards and style guide
└── AUTHORS                     # List of contributors
```

---

## Architecture Overview

### Object-Oriented Hierarchical Design

FluidNC uses a modern C++ object-oriented architecture with:

1. **Hardware Abstraction Layers**: Separates hardware-specific code from control logic
2. **Factory Pattern**: Dynamically creates motor, spindle, and kinematics objects based on configuration
3. **Configuration System**: YAML-based parser and handler system for runtime configuration
4. **Plugin Architecture**: Easy addition of new motor types, spindles, and kinematics

### Key Architectural Principles

- **No RTTI**: Designed to work without Run-Time Type Information (unavailable on ESP32)
- **Configurable Base Class**: Most components inherit from `Configuration::Configurable`
- **Factory Registration**: Components self-register with factories during static initialization
- **Validation**: Configuration validation happens before machine initialization

### Data Flow

```
User Config (YAML) → Tokenizer → Parser → Configuration Handlers →
  → Object Creation (Factories) → Validation → Machine Initialization
```

---

## Core Components

### 1. Configuration System (`FluidNC/src/Configuration/`)

**Purpose**: Parse and process YAML configuration files

**Key Files**:
- `Configurable.h` - Base interface for configurable objects
- `Parser.cpp/h` - YAML parser implementation
- `Tokenizer.cpp/h` - Breaks YAML into tokens
- `GenericFactory.h` - Template-based factory for creating objects
- `ParserHandler.h` - Handles configuration tree building
- `Validator.cpp/h` - Validates configuration correctness

**Key Concepts**:
- Configuration happens through the `group()` method which maps YAML keys to member variables
- Factories use the `InstanceBuilder` pattern for self-registration
- No dynamic_cast due to ESP32 limitations

### 2. Machine Components (`FluidNC/src/Machine/`)

**Purpose**: Define machine structure and components

**Key Components**:
- `MachineConfig.cpp/h` - Main machine configuration class
- `Axes.cpp/h` - Multi-axis management
- `Axis.cpp/h` - Individual axis configuration
- `Motor.cpp/h` - Base motor class
- `Homing.cpp/h` - Homing cycle implementation
- `LimitPin.cpp/h` - Limit switch handling
- `SPIBus.cpp/h`, `I2CBus.cpp/h`, `I2SOBus.cpp/h` - Communication bus abstractions

### 3. Motors (`FluidNC/src/Motors/`)

**Purpose**: Stepper and servo motor drivers

**Supported Motor Types**:
- `StandardStepper` - Basic step/direction stepper
- `StepStick` - Common stepper driver boards
- Trinamic drivers:
  - `TMC2130Driver` (SPI)
  - `TMC2208Driver` (UART)
  - `TMC2209Driver` (UART, most common)
  - `TMC2160Driver`, `TMC5160Driver` (high current)
- `RcServo` - RC servo motors
- `Servo` - Standard servo motors
- `Solenoid` - Solenoid actuators
- `Dynamixel2` - Dynamixel smart servos
- `NullMotor` - Placeholder for unused axes

### 4. Spindles (`FluidNC/src/Spindles/`)

**Purpose**: Control spindles and lasers

**Spindle Types**:
- `PWMSpindle` - PWM-controlled spindles
- `LaserSpindle` - Laser control with safety features
- `RelaySpindle` - Simple on/off relay control
- `OnOffSpindle` - Basic on/off spindle
- `VFDSpindle` - Variable Frequency Drive control (Modbus)
- `BESCSpindle` - Brushless ESC control
- `DacSpindle` - DAC voltage output
- `10vSpindle` - 0-10V analog control
- `HBridgeSpindle` - H-bridge motor control
- `PlasmaSpindle` - Plasma cutter control
- `NullSpindle` - Disabled spindle

**VFD Protocols** (`FluidNC/src/Spindles/VFD/`):
- Huanyang protocol variants
- Modbus RTU communication

### 5. Kinematics (`FluidNC/src/Kinematics/`)

**Purpose**: Transform between cartesian and motor space

**Supported Kinematics**:
- `Cartesian` - Standard XYZ motion (most common)
- `CoreXY` - CoreXY motion system
- `WallPlotter` - Cable-driven wall plotters
- `ParallelDelta` - Delta robot kinematics
- `Midtbot` - SCARA-like robot

### 6. Motion Control

**Key Files**:
- `GCode.cpp/h` - G-code parser and interpreter
- `MotionControl.cpp/h` - Motion planning and execution
- `Planner.cpp/h` - Motion planner (trajectory planning)
- `Stepper.cpp/h` - Stepper pulse generation
- `Stepping.cpp/h` - Stepping engine abstraction
- `Limits.cpp/h` - Limit switch handling
- `Probe.cpp/h` - Probing operations
- `Jog.cpp/h` - Jogging mode

### 7. Web Interface (`FluidNC/src/WebUI/`)

**Purpose**: Web-based control interface

**Components**:
- `WSChannel.cpp/h` - WebSocket communication
- `TelnetServer.cpp/h` - Telnet access
- `WebClient.cpp/h` - HTTP client
- `WebCommands.cpp` - Web command handlers
- `NotificationsService.cpp/h` - Push notifications
- `OTA.cpp` - Over-the-air firmware updates
- `Authentication.cpp/h` - User authentication
- `Mdns.cpp/h` - mDNS/Bonjour support

### 8. Pin Management (`FluidNC/src/Pins/`)

**Purpose**: Hardware pin abstraction and mapping

**Features**:
- GPIO pin mapping
- I2S output pins
- Virtual pins
- Pin attributes (pullup, pulldown, invert)
- Pin parser for configuration strings

---

## Build System

### PlatformIO Configuration

**Main Configuration**: `platformio.ini`

**Build Environments**:
1. **wifi** (default) - ESP32 with WiFi support
2. **bt** - ESP32 with Bluetooth support
3. **noradio** - ESP32 without wireless
4. **wifi_s3** - ESP32-S3 with WiFi
5. **bt_s3** - ESP32-S3 with Bluetooth
6. **noradio_s3** - ESP32-S3 without wireless
7. **debug** - Debug build with symbols
8. **tests** - Native unit tests

### Key Build Settings

- **Platform**: espressif32 (Arduino framework)
- **C++ Standard**: C++17 (`-std=gnu++17`)
- **Upload Speed**: 921600 baud
- **Monitor Speed**: 115200 baud
- **Partition Scheme**: Custom (`min_littlefs.csv`)
- **Filesystem**: LittleFS

### Dependencies

**External Libraries**:
- `TMCStepper` (>=0.7.0, <1.0.0) - Trinamic driver support
- `ESP8266 and ESP32 OLED driver for SSD1306` - Display support
- `arduinoWebSockets` - WebSocket communication (custom fork)
- `WiFi` library (custom fork)
- ESP32 framework libraries (ArduinoOTA, DNSServer, ESPmDNS, WebServer)

### Building the Project

```bash
# Build default (wifi) environment
pio run

# Build specific environment
pio run -e wifi_s3

# Upload firmware
pio run -t upload

# Upload filesystem
pio run -t uploadfs

# Run tests
pio test
```

### Version Management

The build system uses `git-version.py` to automatically inject git version information into the firmware.

---

## Configuration System

### YAML-Based Configuration

FluidNC uses YAML files to define machine configurations. Users don't need to recompile firmware.

### Configuration File Structure

```yaml
name: Machine Name
board: Board Type
meta: Description and metadata

# Stepping configuration
stepping:
  engine: I2S_STREAM
  idle_ms: 255
  pulse_us: 4

# Axes configuration
axes:
  x:
    steps_per_mm: 800
    max_rate_mm_per_min: 5000
    acceleration_mm_per_sec2: 100
    max_travel_mm: 300
    motor0:
      # Motor configuration

# Spindle configuration
spindle_type:
  pwm_hz: 5000
  output_pin: gpio.2
  enable_pin: gpio.4

# Additional components...
```

### Configuration Loading

1. Default config: `/localfs/config.yaml`
2. Can specify alternate config: `$Config/Filename=myconfig.yaml`
3. Multiple configs can be stored on the ESP32
4. Upload via USB serial or WiFi

### Example Configurations

Example configs are maintained in a separate repository:
- Repository: https://github.com/bdring/fluidnc-config-files
- Covers various machine types and configurations

---

## Operational States and Control

FluidNC implements a sophisticated state machine and control system for safe CNC operation. This section describes how the firmware handles operational states, pause/resume, emergency stops, and pin-based control.

### Machine States

**File**: `FluidNC/src/State.h`

FluidNC operates in distinct states that determine what operations are allowed:

| State | Description | Allowed Operations |
|-------|-------------|-------------------|
| `Idle` | Machine ready, no motion | Accept G-code, homing, jogging, settings |
| `Alarm` | Error condition, motion locked | Only unlock ($X), settings, and status queries |
| `Cycle` | Running G-code program | Pause, stop, override commands |
| `Hold` | Decelerating to pause | Wait for complete stop |
| `Held` | Paused, motion stopped | Resume (~), cancel, jog |
| `Jog` | Jogging mode active | Cancel jog, feed hold |
| `Homing` | Homing cycle running | Reset only |
| `SafetyDoor` | Safety door opened, parking | Close door to resume |
| `Sleep` | Low power mode | Cycle start to wake |
| `CheckMode` | G-code validation only | No motion, test programs |
| `ConfigAlarm` | Configuration error | Fix config, can't operate |
| `Critical` | Critical error | Only reset (Ctrl-X) |

**State Transitions:**
```
Idle → Cycle (start job)
Cycle → Hold (feed hold !)
Hold → Held (motion stopped)
Held → Cycle (cycle start ~)
Any → Alarm (limit hit, error)
Alarm → Idle (unlock $X)
```

### Realtime Commands

**File**: `FluidNC/src/RealtimeCmd.h`

Realtime commands are processed immediately, even during G-code execution:

| Command | Character | Function | Description |
|---------|-----------|----------|-------------|
| **Feed Hold** | `!` | Pause | Decelerates and pauses motion |
| **Cycle Start** | `~` | Resume | Resumes paused motion |
| **Reset** | `Ctrl-X` (0x18) | Emergency Stop | Hard reset, stops all motion |
| **Status Report** | `?` | Query Status | Returns current state |
| **Safety Door** | 0x84 | Door Opened | Triggers parking sequence |
| **Jog Cancel** | 0x85 | Cancel Jog | Stops jogging motion |

**Override Commands:**
- Feed Rate: 0x90-0x94 (reset, coarse +/-, fine +/-)
- Rapid Rate: 0x95-0x98 (100%, 50%, 25%)
- Spindle Speed: 0x99-0x9E (reset, coarse +/-, fine +/-, stop)
- Coolant: 0xA0-0xA1 (flood toggle, mist toggle)

### Control Input Pins

**File**: `FluidNC/src/Control.cpp`

Physical pins can trigger control functions. Configure in YAML under `control:` section.

**Available Control Pins:**

| Pin Name | Letter | Function | Behavior |
|----------|--------|----------|----------|
| `safety_door_pin` | D | Safety Door | Opens door, triggers parking |
| `reset_pin` | R | Hardware Reset | Same as Ctrl-X |
| `feed_hold_pin` | H | Pause | Hardware feed hold |
| `cycle_start_pin` | S | Resume | Hardware cycle start |
| `estop_pin` | E | Emergency Stop | Triggers fault alarm |
| `fault_pin` | F | External Fault | Triggers fault alarm |
| `macro0_pin` - `macro3_pin` | 0-3 | User Macros | Run configured macro |
| `homing_button_pin` | O | Start Homing | Initiates homing cycle |

**Configuration Example:**
```yaml
control:
  safety_door_pin: gpio.35:low:pu
  feed_hold_pin: gpio.36:low:pu
  cycle_start_pin: gpio.37:low:pu
  reset_pin: gpio.34:low:pu
  estop_pin: gpio.39:low:pu
```

**Pin Attributes:**
- `:low` - Active low (triggered when grounded)
- `:high` - Active high (triggered when high)
- `:pu` - Enable internal pull-up resistor
- `:pd` - Enable internal pull-down resistor

**Important Notes:**
- E-Stop and Fault pins block homing and unlock until released
- All control pins are checked at startup; active pins prevent startup
- Safety door pin triggers parking sequence (if enabled)

### Status Output Pins

**File**: `FluidNC/src/Status_outputs.cpp`

FluidNC can drive output pins based on machine state for tower lights, indicators, etc.

**Available Status Outputs:**

| Pin Name | Active When | Use Case |
|----------|-------------|----------|
| `idle_pin` | State = Idle | Green light: Ready |
| `run_pin` | State = Run/Cycle | Amber light: Running |
| `hold_pin` | State = Hold/Held | Amber flashing: Paused |
| `alarm_pin` | State = Alarm | Red light: Error |
| `door_pin` | State = SafetyDoor | Red flashing: Door open |

**Configuration Example:**
```yaml
status_outputs:
  report_interval_ms: 500
  idle_pin: gpio.25
  run_pin: gpio.26
  hold_pin: gpio.27
  alarm_pin: gpio.32
  door_pin: gpio.33
```

**How It Works:**
- Status outputs module subscribes to status reports
- Parses machine state from status strings
- Updates output pins accordingly
- Configurable update interval (100-5000 ms)

### Pause and Resume Operations

#### Feed Hold (Pause)

**Trigger Methods:**
1. Serial command: `!` character
2. Control pin: `feed_hold_pin`
3. Web UI: Pause button

**Behavior:**
```
State: Cycle → Hold → Held
1. Receive feed hold command
2. Decelerate motion (State: Hold)
3. Motion stops (State: Held)
4. Spindle continues running (unless overridden)
5. Position maintained
```

**During Hold State:**
- Can issue status queries (`?`)
- Can adjust overrides (feed, rapid, spindle)
- Can jog (if in Held state)
- Can cancel job
- Cannot add to G-code queue

#### Cycle Start (Resume)

**Trigger Methods:**
1. Serial command: `~` character
2. Control pin: `cycle_start_pin`
3. Web UI: Resume button

**Behavior:**
```
State: Held → Cycle
1. Receive cycle start command
2. Spindle spins up (if needed)
3. Motion resumes from paused point
4. Returns to normal operation
```

### Emergency Stop (E-Stop)

FluidNC supports multiple levels of stopping:

#### Soft E-Stop (Feed Hold)
- **Command**: `!` or feed_hold_pin
- **Behavior**: Controlled deceleration, maintains position
- **Recovery**: Resume with `~`
- **Use**: Normal pause during operation

#### Hard E-Stop (Fault/E-Stop Pin)
- **Command**: `estop_pin` activated
- **Behavior**:
  - Triggers immediate alarm (ExecAlarm::ControlPin)
  - Stops motion
  - Spindle stops
  - Enters Alarm state
  - Position may be lost if deceleration incomplete
- **Recovery**:
  1. Clear physical e-stop
  2. Issue `$X` to unlock
  3. Re-home if position lost

#### Reset (Critical Stop)
- **Command**: `Ctrl-X` or reset_pin
- **Behavior**:
  - Immediate reset of firmware
  - All motion stops instantly
  - State reset to Idle (with Alarm if needed)
  - Position lost
  - All settings retained
- **Recovery**:
  1. Check why reset was needed
  2. Home machine
  3. Resume operations

### Alarm States and Recovery

**File**: `FluidNC/src/Protocol.h` - ExecAlarm enum

**Alarm Types:**

| Alarm Code | Name | Cause | Recovery |
|------------|------|-------|----------|
| 1 | HardLimit | Limit switch triggered | Clear obstruction, `$X`, home |
| 2 | SoftLimit | Motion beyond soft limits | `$X`, check program |
| 3 | AbortCycle | User aborted | `$X` |
| 4-5 | Probe Fail | Probe operation failed | Fix probe, `$X` |
| 6-9 | Homing Fail | Homing cycle failed | Check switches, `$X`, retry |
| 10 | SpindleControl | Spindle control error | Check spindle, `$X` |
| 11 | ControlPin | E-stop/fault active at startup | Clear pin, `$X` |
| 13 | HardStop | External stop command | `$X` |
| 14 | Unhomed | Move attempted while unhomed | Home machine |

**Unlocking from Alarm:**

```bash
$X                    # Unlock command
```

**What $X Does:**
1. Checks if E-Stop/Fault pins are clear
2. Sets all axes to "homed" state (position may be incorrect!)
3. Transitions from Alarm → Idle
4. Runs `after_unlock` macro (if configured)
5. Releases motor locks

**Important:** After `$X`, re-home the machine to establish accurate position!

### Safety Door and Parking

**Files**: `FluidNC/src/Parking.h`, `FluidNC/src/Protocol.cpp`

When safety door opens, FluidNC can automatically park the tool.

**Configuration:**
```yaml
parking:
  enable: true
  axis: Z                          # Usually Z axis
  target_mpos_mm: -5.0            # Park position
  pullout_distance_mm: 5.0        # Retract before lateral move
  rate_mm_per_min: 800.0          # Park speed
  pullout_rate_mm_per_min: 250.0  # Retract speed
```

**Parking Sequence:**

1. Safety door opens → Safety door pin triggers
2. **State: Cycle → SafetyDoor**
3. Motion decelerates and stops
4. Spindle stops
5. **Parking Motion:**
   - Retract on parking axis (pullout_distance)
   - Move to park position (target_mpos)
6. De-energize motors (optional)
7. Wait for door to close

**Resume Sequence:**

1. Door closes
2. User presses Cycle Start (`~`)
3. **Unparking Motion:**
   - Move to position before retract
   - Move to original position
4. **State: SafetyDoor → Cycle**
5. Spindle restarts
6. Motion resumes

**Safety Features:**
- Door opening during parking cancels and re-parks
- Motion cannot resume while door is open
- Spindle and coolant states are preserved and restored

### Soft Limits vs Hard Limits

#### Hard Limits (Physical)

**Configuration:**
```yaml
axes:
  x:
    motor0:
      limit_neg_pin: gpio.35:low:pu
      limit_pos_pin: gpio.36:low:pu
```

**Behavior:**
- Physical switch triggers immediately
- Triggers ExecAlarm::HardLimit
- Motion stops (may not be instantaneous)
- Position may be lost
- Requires `$X` and re-homing

**Use:** Protect against mechanical over-travel

#### Soft Limits (Software)

**Configuration:**
```yaml
axes:
  x:
    max_travel_mm: 300
    homing:
      mpos_mm: 0
```

**Behavior:**
- Checked during motion planning
- Prevents moves beyond defined workspace
- Triggers ExecAlarm::SoftLimit before motion starts
- Position maintained
- Requires `$X` to clear

**Use:** Prevent programs from exceeding workspace

**Enabling Soft Limits:**
```bash
$Limits/Soft=On       # Enable soft limit checking
```

**Requirements:**
- Machine must be homed
- `max_travel_mm` defined for each axis
- Homing positions (`mpos_mm`) configured

### Event-Triggered Macros

**File**: `FluidNC/src/Machine/Macros.h`

Macros are G-code sequences that run automatically on events.

**Available Macro Triggers:**

| Macro | Trigger | Use Case |
|-------|---------|----------|
| `startup_line0` | Power on/reset | Initialize machine state |
| `startup_line1` | Power on/reset | Second initialization line |
| `after_reset` | After reset | Post-reset setup |
| `after_homing` | Homing complete | Move to safe position |
| `after_unlock` | After $X unlock | Run verification routine |
| `macro0` - `macro3` | Pin/button/command | User-defined operations |

**Configuration Example:**
```yaml
macros:
  startup_line0: G21 G90  # Metric, absolute mode
  startup_line1: M5       # Spindle off
  after_homing: G0 X10 Y10 Z5  # Move to safe position
  after_unlock: G28       # Return to park position
  macro0: G28 & G0X0Y0    # Park and zero
  macro1: M3 S1000        # Start spindle
```

**Macro Syntax:**
- Standard G-code commands
- Multiple commands separated by `&`
- Cannot contain comments
- Executed as if typed manually

**Triggering Macros:**
- Pin: Configure `macro0_pin` to `macro3_pin`
- Serial: Send 0x87-0x8A characters
- After events: Automatic

### User Inputs and Outputs

**Files**: `FluidNC/src/Machine/UserInputs.h`, `FluidNC/src/Machine/UserOutputs.h`

FluidNC supports user-controllable I/O for custom automation.

#### User Digital Outputs

**Configuration:**
```yaml
user_outputs:
  digital_output_0: gpio.16
  digital_output_1: gpio.17
  digital_output_2: gpio.18
  digital_output_3: gpio.19
```

**G-code Control:**
```gcode
M62 P0          # Turn on digital output 0
M63 P0          # Turn off digital output 0
M64 P1          # Turn on output 1 immediately
M65 P1          # Turn off output 1 immediately
```

**Uses:**
- Dust collection control
- Vacuum hold-down
- Part ejectors
- Indicator lights
- Relay control

#### User Analog Outputs

**Configuration:**
```yaml
user_outputs:
  analog_output_0_pin: gpio.25
  analog_output_0_hz: 5000
  analog_output_1_pin: gpio.26
  analog_output_1_hz: 1000
```

**G-code Control:**
```gcode
M67 E0 Q50.0    # Set analog output 0 to 50%
M67 E1 Q75.5    # Set analog output 1 to 75.5%
```

**Uses:**
- Variable speed fans
- PWM controlled devices
- Analog control signals

#### User Digital Inputs

**Configuration:**
```yaml
user_inputs:
  digital_input_0_pin: gpio.34
  digital_input_0_name: "probe_trigger"
  digital_input_1_pin: gpio.35
```

**G-code Reading:**
```gcode
M66 P0 L0       # Read digital input 0 immediately
M66 P1 L3 Q5.0  # Wait up to 5 sec for input 1 high
```

**Uses:**
- External triggers
- Tool presence detection
- Material sensors
- Safety interlocks

#### User Analog Inputs

**Configuration:**
```yaml
user_inputs:
  analog_input_0_pin: gpio.36
  analog_input_0_name: "temperature"
```

**G-code Reading:**
```gcode
M66 E0 L0       # Read analog input 0
```

**Note:** ESP32 GPIO analog inputs read 0-3.3V as 0-100%

### Practical Examples

#### Example 1: Emergency Stop Setup

```yaml
control:
  estop_pin: gpio.39:low:pu

status_outputs:
  alarm_pin: gpio.32

macros:
  after_unlock: G28 & G0Z5  # Safe Z height after unlock
```

**Operation:**
1. E-stop pressed → alarm_pin turns on (red light)
2. Machine enters Alarm state
3. User fixes issue, releases e-stop
4. Send `$X` to unlock
5. Macro runs: returns to park, lifts Z
6. Home machine: `$H`

#### Example 2: Pause/Resume with Door

```yaml
control:
  safety_door_pin: gpio.35:low:pu

parking:
  enable: true
  axis: Z
  target_mpos_mm: -5.0
  pullout_distance_mm: 3.0

status_outputs:
  door_pin: gpio.33
  hold_pin: gpio.27
```

**Operation:**
1. Job running, door opens
2. door_pin activates (red light)
3. Motion pauses, Z retracts 3mm, moves to Z=-5
4. User loads material, closes door
5. Press cycle start or `~`
6. Z returns to work position
7. Job resumes

#### Example 3: Dust Collection Control

```yaml
user_outputs:
  digital_output_0: gpio.16  # Dust collector relay

macros:
  macro0: M62P0 & M3S10000           # Start DC + Spindle
  macro1: M5 & G4P2 & M63P0          # Stop spindle, wait, stop DC
  after_reset: M63P0                 # Ensure DC off on reset
```

**Operation:**
- Press macro0 button → Dust collector + spindle on
- Press macro1 button → Spindle off, wait 2 sec, dust collector off
- On reset → Dust collector guaranteed off

---

## Development Guidelines

### Coding Style

**File**: `CodingStyle.md`

**Key Rules**:

1. **Formatting**: Use `.clang-format` (automatic formatting)
2. **Naming Conventions**:
   - Classes/Namespaces: `CamelCase`
   - Member functions: `snake_case`
   - Member variables: `_snake_case` (leading underscore)
3. **File Organization**:
   - One class per file
   - Filename matches class name
   - Header guard: `#pragma once`
4. **Includes**:
   - System/library: `<...>`
   - Local files: `"..."`
   - CPP file includes its header first
5. **Namespace Usage**:
   - No `using namespace` in headers (except in function bodies)
   - Conservative use in .cpp files

### Adding New Components

#### Adding a New Motor Type

1. Create files: `FluidNC/src/Motors/MyMotor.cpp` and `.h`
2. Inherit from `MotorDriver` base class
3. Implement required methods
4. Register with factory in .cpp file:
   ```cpp
   namespace {
       MotorFactory::InstanceBuilder<MyMotor> registration("my_motor");
   }
   ```
5. Implement `group()` method for configuration

#### Adding a New Spindle Type

1. Create files: `FluidNC/src/Spindles/MySpindle.cpp` and `.h`
2. Inherit from `Spindle` base class
3. Implement required methods (init, setState, etc.)
4. Register with `SpindleFactory`

#### Adding a New Kinematics System

1. Create files: `FluidNC/src/Kinematics/MyKinematics.cpp` and `.h`
2. Inherit from `Kinematics` base class
3. Implement transforms: `cartesian_to_motors()` and `motors_to_cartesian()`
4. Register with `KinematicsFactory`

### Testing

**Unit Tests**: Located in `FluidNC/tests/`

**Test Framework**: Google Test

**Running Tests**:
```bash
pio test -e tests
```

**Fixture Tests**: Located in `fixture_tests/` for integration testing

---

## Installation and Usage

### For End Users

1. **Install Firmware**:
   - Use installation scripts in `install_scripts/`
   - Windows: `install_scripts/win64/`
   - Linux/Mac: `install_scripts/posix/`
   - Or use pre-built releases from GitHub

2. **Create Configuration**:
   - Start with example config for your machine type
   - Modify YAML file for your specific hardware
   - Validate configuration

3. **Upload Configuration**:
   - Via USB: Use FluidTerm or other serial terminal
   - Via WiFi: Use web interface
   - Command: `$LocalFS/Upload`

4. **Connect and Control**:
   - **Serial**: USB connection at 115200 baud
   - **WiFi**: Connect to AP or configure STA mode
   - **Web UI**: Browse to ESP32's IP address
   - **GCode Senders**: Compatible with Grbl senders

### FluidTerm Terminal

**Location**: `fluidterm/`

A terminal application specifically designed for FluidNC interaction.

### Configuration Commands

- `$Config/Filename=name.yaml` - Set active config
- `$$` - Show current settings
- `$H` - Home machine
- `$X` - Clear alarm
- `[ESP420]` - List files
- `[ESP700]` - Show WiFi status

---

## Contributing

### How to Contribute

1. **Fork the Repository**
2. **Create a Feature Branch**
3. **Follow Coding Style**: Use clang-format
4. **Write Tests**: Add tests for new features
5. **Test on Hardware**: If possible, test on actual ESP32
6. **Submit Pull Request**
7. **Documentation**: Update docs for user-facing changes

### Contribution Guidelines

- Maintain Grbl compatibility where possible
- Keep code modular and extensible
- Use the configuration system for new settings
- Add example configurations for new features
- Update relevant documentation

### Key Contributors

See `AUTHORS` file for the list of contributors. Key contributors include:

- Bart Dring - Project management, hardware, design
- Mitch Bradley - Settings architecture, YAML configuration
- Stefan de Bruijn - YAML configuration, VFD spindles
- Luc Lebosse - WebUI
- And many more (see AUTHORS file)

---

## Key Files and Directories

### Essential Files

| File | Purpose |
|------|---------|
| `platformio.ini` | Build configuration |
| `README.md` | Project overview |
| `LICENSE` | GPLv3 license text |
| `CodingStyle.md` | Coding standards |
| `AUTHORS` | Contributor list |
| `FluidNC/src/Main.cpp` | Firmware entry point |
| `FluidNC/src/GCode.cpp` | GCode parser |
| `FluidNC/src/Machine/MachineConfig.cpp` | Machine configuration loader |

### Documentation Files

| File | Topic |
|------|-------|
| `FluidNC/src/Configuration/_Overview.md` | Configuration system |
| `FluidNC/src/Configuration/Parser.md` | Parser details |
| `FluidNC/src/Configuration/GenericFactory.md` | Factory pattern |
| `FluidNC/src/Stepper.md` | Stepper implementation |
| `FluidNC/src/Motors/Dynamixel2.md` | Dynamixel motor driver |
| `VisualStudio.md` | Visual Studio setup |

### Build Artifacts

- `firmware.nm` - Symbol map
- `.pio/` - PlatformIO build directory (generated)
- `.vscode/` - VS Code configuration (generated)

---

## Resources

### Official Resources

- **Wiki**: http://wiki.fluidnc.com
- **Config Files Repository**: https://github.com/bdring/fluidnc-config-files
- **Discord**: Ask for invite (see README)
- **GitHub Issues**: Report bugs and feature requests

### Related Projects

- **Grbl**: https://github.com/gnea/grbl (original inspiration)
- **Grbl_ESP32**: Previous generation firmware
- **ESP3D-WEBUI**: https://github.com/luc-github/ESP3D-WEBUI (WebUI basis)

### Learning Resources

- **Grbl Documentation**: Understanding CNC control principles
- **ESP32 Arduino**: https://github.com/espressif/arduino-esp32
- **PlatformIO**: https://docs.platformio.org/

---

## Quick Reference

### Common Tasks

| Task | Command/Location |
|------|------------------|
| Build firmware | `pio run` |
| Upload firmware | `pio run -t upload` |
| View serial monitor | `pio device monitor` |
| Run tests | `pio test` |
| Format code | Apply clang-format |
| Upload config | WebUI or serial commands |
| Home machine | `$H` |
| Check status | `?` |
| Reset | `Ctrl-X` |

### Project Statistics

- **Primary Language**: C++17
- **Target Platform**: ESP32
- **Core Source Lines**: ~20,000 lines (core files only)
- **License**: GPLv3
- **Configuration**: YAML
- **Communication**: Serial, WiFi, Bluetooth, WebSocket, Telnet

---

## Architecture Diagram (Conceptual)

```
┌─────────────────────────────────────────────────────────┐
│                    User Interfaces                      │
│  (Web UI, Serial, Telnet, WebSocket, GCode Senders)   │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Protocol & Command Layer                   │
│        (GCode Parser, Commands, Protocol)              │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Motion Control Layer                       │
│     (Motion Control, Planner, Stepper, Limits)         │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Kinematics Layer                           │
│   (Cartesian, CoreXY, Delta - Coordinate Transform)    │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Machine Components Layer                   │
│        (Axes, Motors, Spindles, Tool Changers)         │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Hardware Abstraction Layer                 │
│     (Pins, I2S, SPI, I2C, UART, Motor Drivers)         │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              ESP32 Hardware                             │
│         (GPIO, Timers, PWM, Communication)             │
└─────────────────────────────────────────────────────────┘
```

---

## Conclusion

FluidNC is a comprehensive, modern CNC firmware that leverages the ESP32's capabilities to provide a flexible, user-friendly CNC control solution. Its object-oriented architecture, YAML configuration system, and extensive hardware support make it suitable for a wide range of CNC applications.

The codebase is well-structured, with clear separation of concerns and a modular design that facilitates extension and customization. Whether you're an end user looking to configure a machine or a developer wanting to add new features, FluidNC provides the tools and documentation to succeed.

For more information, visit the wiki at http://wiki.fluidnc.com or join the Discord community.

---

**Document Version**: 1.0
**Last Updated**: 2025-11-06
**FluidNC Version**: Based on current main branch
