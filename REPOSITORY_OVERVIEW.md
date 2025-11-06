# FluidNC Repository Overview

## Table of Contents
1. [Introduction](#introduction)
2. [Project Purpose](#project-purpose)
3. [Repository Structure](#repository-structure)
4. [Architecture Overview](#architecture-overview)
5. [Core Components](#core-components)
6. [Build System](#build-system)
7. [Configuration System](#configuration-system)
8. [Development Guidelines](#development-guidelines)
9. [Installation and Usage](#installation-and-usage)
10. [Contributing](#contributing)
11. [Key Files and Directories](#key-files-and-directories)
12. [Resources](#resources)

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
