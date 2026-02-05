# FluidNC Copilot Instructions

## Project Overview
**FluidNC** is a CNC firmware for ESP32 optimized for multi-tool machines (laser + spindle, tool changers, etc.). It's a successor to Grbl_ESP32 with hierarchical OOP architecture, configuration-driven machine definitions (YAML), and web-based UI.

## Architecture Essentials

### Core Philosophy
- **Config-driven**: Machine definitions are YAML files, not compiled code. No need to recompile for hardware changes.
- **Hardware abstraction**: Motors, spindles, steppers, IO pins are pluggable components.
- **Grbl-compatible**: G-code protocol and day-to-day operations unchanged; `$` settings replaced with readable config files.
- **Async web server**: Uses AsyncTCP + ESPAsyncWebServer (not standard WebServer) for robustness to network disconnections.

### Major Components

| Component | Location | Purpose |
|-----------|----------|---------|
| **Machine** | `src/Machine/` | Axes, motors, homing, IO abstraction (SPIBus, I2CBus, etc.) |
| **Stepping** | `src/Stepping.cpp/.h` | Stepper engine abstraction—timing, step/dir pulse generation |
| **Stepper** | `src/Stepper.cpp/.h` | Motion control ISR; interfaces with Planner → Stepping |
| **Protocol** | `src/Protocol.cpp/.h` | Main state machine; execution loop; real-time command handling |
| **Motors** | `src/Motors/` | StandardStepper, Trinamics (TMC2209, etc.), Servo, Solenoid, etc. |
| **GCode** | `src/GCode.cpp` | RS274/NGC parser; command execution bridge |
| **WebUI** | `src/WebUI/` | AsyncWeb handlers, authentication, WebDAV, OTA updates |
| **Configuration** | `src/Configuration/` | Configurable base class; YAML parsing; factory registration |

### Data Flow: G-Code → Motion
1. **Protocol** reads G-code from channel (serial/WebSocket/telnet)
2. **GCode** parser extracts M/G commands, updates plan
3. **Planner** builds motion segments (acceleration profiles)
4. **Stepper** ISR executes motion; calls `Stepping::step(step_mask, dir_mask)`
5. **Stepping** multiplexes to **Motor**s via pluggable stepping engine (Timed/RMT/I2S)
6. **Motor** (e.g., StandardStepper) toggles step/dir pins

### Naming Conventions (Enforced by .clang-format)
- **Namespaces & Classes**: `CamelCase` (e.g., `Machine::Axes`, `MotorDrivers::StandardStepper`)
- **Methods**: `snake_case` (e.g., `init()`, `set_direction()`)
- **Member Variables**: `_leading_underscore_snake_case` (e.g., `_step_pin`, `_run_current`)
- **Includes**: Use `<...>` for system/library, `"..."` for local files
- **Header Guards**: Use `#pragma once` (not `#ifndef`)

## Build & Test

### Build Targets (PlatformIO)
```bash
platformio run --environment wifi         # ESP32 + WiFi + WebUI
platformio run --environment noradio      # ESP32 only
platformio run --environment esp32s3      # ESP32-S3
platformio run --target clean --environment wifi  # Clean
```

### POSIX Build (Posix branch)
For testing on macOS/Linux without hardware:
```bash
platformio run --environment posix
```

### Key Build Flags
- Async web server options in `platformio.ini` (chunk handling, queue size, TCP priority)
- Stepping engine: `TIMED`, `RMT`, `I2S_STATIC`, `I2S_STREAM` (configurable per machine)

### Tests
Located in `FluidNC/tests/`: 
- `CompletionTest.cpp`, `PinOptionsParserTest.cpp` — Unit tests
- Run via PlatformIO test environment or linked into main binary

## Critical Patterns & Workflows

### 1. Adding a New Motor Driver
1. Create class in `src/Motors/` (e.g., `MyMotor.h/cpp`) inheriting from `MotorDriver`
2. Implement `init()`, `set_disable()`, `validate()`
3. Register in `group()` handler for config system:
   ```cpp
   void group(Configuration::HandlerBase& handler) override {
       handler.item("step_pin", _step_pin);
       handler.item("run_current", _run_current);  // if applicable
   }
   ```
4. Add registration macro (in cpp file):
   ```cpp
   namespace {
       GenericFactory<MotorDriver> myMotor(MotorDriver::factory, "MyMotor", 
           nullptr, [](){ return new MyMotor("MyMotor"); });
   }
   ```

### 2. Stepping Engine Integration
- **Stepping engines** are low-level pulse generators (see `include/Driver/step_engine.h`)
- StandardStepper delegates to `Stepping::step()` → engine-specific pin toggles
- For new engine: implement `step_engine_t` interface; register via `find_engine()`

### 3. Configuration System
- All hardware-level configs parse from YAML via `Configuration::Configurable`
- Base class provides `group()` handler pattern for nested config items
- Example: `Machine::Axes` defines axes; each axis has motors; each motor is configurable
- Changes don't require recompile; upload new YAML via web UI or serial

### 4. WebUI & Async Server
- **Not** standard Arduino WebServer—uses ESPAsyncWebServer (custom fork with WebDAV)
- WebUI code only compiles with `+<WebUI>` build filter (config-controlled)
- Async prevents hangs on abrupt client disconnections during jobs
- REST endpoints in `src/WebUI/WebUIServer.cpp`, WebSocket via `src/WebUI/WSChannel.cpp`

### 5. Channel Abstraction
- Channels (Serial, WebSocket, Telnet, HTTP) inherit from `Channel` base
- Protocol loops over all channels; any can send commands, read responses
- Enables multi-client control (web + serial simultaneously)

## Code Review Checklist

- **Naming**: Follows CamelCase (classes), snake_case (methods, members with `_`)
- **Includes**: Correct quotes (`"local"`) and angle brackets (`<system>`); cpp includes header first
- **Hardware abstraction**: New features use `Machine::` or `MotorDrivers::` namespaces, not hardcoded pins
- **Config system**: Configurable features implement `group()` handler
- **No blocking**: ISR-context code (Stepping, Stepper) avoids allocations, logging
- **Clang-format**: Run before commit (IDE auto-format or CLI: `clang-format -i file.cpp`)

## Common Tasks

| Task | Key Files | Example Command |
|------|-----------|-----------------|
| Add spindle type | `src/Spindles/` | See `BESCSpindle.cpp` for reference |
| Add motor driver | `src/Motors/` | See StandardStepper pattern above |
| Fix stepping timing | `src/Stepping.cpp`, `src/Stepper.cpp` | Adjust `_pulseUsecs`, `_directionDelayUsecs` |
| Web UI endpoint | `src/WebUI/WebUIServer.cpp` | Add handler + registration |
| Axis-specific init | `src/Machine/Axes.cpp` | Modify `init()` loop |
| G-code command | `src/GCode.cpp` | Add case in switch, call appropriate handler |

## Essential Files to Know
- **Machine definition**: `FluidNC/include/Config.h` (MAX_N_AXIS, MAX_MOTORS_PER_AXIS)
- **Build config**: `platformio.ini`, `FluidNC/CMakeLists.txt`
- **Stepping timing**: `src/Stepping.h` for constants (fStepperTimer)
- **Style guide**: `CodingStyle.md` (clang-format, naming rules)
- **Motor hierarchy**: `src/Motors/MotorDriver.h` (base class docs)

## Notes
- Grbl historical code (© Sungeun K. Jeon) is in many modules—respect copyright comments
- IRAM_ATTR functions in ISR context (Stepping, Stepper)—be cautious with changes
- Async server fork: custom WebDAV branch; check `platformio.ini` git URL if updating
